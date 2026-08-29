#include "media/sacd/SacdStream.h"

#include "media/sacd/SacdProbe.h"

#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace md::media::sacd {
namespace {

struct Entry {
    QString isoPath;
};

QMutex g_mutex;
QHash<quint64, Entry> g_registry;
quint64 g_nextToken = 1;
std::atomic<bool> g_streamFailed{false};

QString lookupPath(quint64 token) {
    QMutexLocker lock(&g_mutex);
    const auto it = g_registry.constFind(token);
    return it == g_registry.constEnd() ? QString() : it->isoPath;
}

// ---- 一条流 = 一个 helper 子进程 ----

class Session {
public:
    ~Session() { stop(); }

    bool start(const QString& exe) {
#ifdef _WIN32
        (void)exe;
        return false; // Windows 侧的进程启动归 T8，届时换 CreateProcess
#else
        int toChild[2], fromChild[2];
        if (pipe(toChild) != 0)
            return false;
        if (pipe(fromChild) != 0) {
            ::close(toChild[0]);
            ::close(toChild[1]);
            return false;
        }
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, toChild[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&fa, fromChild[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&fa, toChild[1]);
        posix_spawn_file_actions_addclose(&fa, fromChild[0]);

        const QByteArray path = exe.toLocal8Bit();
        char* argv[] = {const_cast<char*>(path.constData()), nullptr};
        const int rc = posix_spawn(&pid_, path.constData(), &fa, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&fa);
        ::close(toChild[0]);
        ::close(fromChild[1]);
        if (rc != 0) {
            ::close(toChild[1]);
            ::close(fromChild[0]);
            pid_ = -1;
            return false;
        }
        in_ = toChild[1];
        out_ = fromChild[0];
        return true;
#endif
    }

    void stop() {
#ifndef _WIN32
        if (in_ >= 0) {
            ::close(in_);
            in_ = -1;
        }
        if (out_ >= 0) {
            ::close(out_);
            out_ = -1;
        }
        if (pid_ > 0) {
            int status = 0;
            // 关掉 stdin 后 helper 会自己走到 EOF 退出；等一小会儿，真赖着不走就杀。
            for (int i = 0; i < 50; ++i) {
                if (waitpid(pid_, &status, WNOHANG) == pid_) {
                    pid_ = -1;
                    return;
                }
                usleep(2000);
            }
            kill(pid_, SIGKILL);
            waitpid(pid_, &status, 0);
            pid_ = -1;
        }
#endif
    }

    bool alive() const { return in_ >= 0 && out_ >= 0; }

    bool writeLine(const QByteArray& line) {
#ifdef _WIN32
        (void)line;
        return false;
#else
        QByteArray buf = line;
        buf.append('\n');
        const char* p = buf.constData();
        qsizetype left = buf.size();
        while (left > 0) {
            const ssize_t n = ::write(in_, p, size_t(left));
            if (n <= 0)
                return false;
            p += n;
            left -= n;
        }
        return true;
#endif
    }

    bool readExact(char* buf, qsizetype n) {
#ifdef _WIN32
        (void)buf;
        (void)n;
        return false;
#else
        while (n > 0) {
            const ssize_t got = ::read(out_, buf, size_t(n));
            if (got <= 0)
                return false;
            buf += got;
            n -= got;
        }
        return true;
#endif
    }

    QByteArray readLine() {
        QByteArray line;
        char c = 0;
        while (readExact(&c, 1)) {
            if (c == '\n')
                return line;
            line.append(c);
            if (line.size() > (1 << 20))
                break;
        }
        return {};
    }

private:
#ifndef _WIN32
    pid_t pid_ = -1;
#endif
    int in_ = -1;
    int out_ = -1;
};

struct Stream {
    Session session;
    int dumpFd = -1; // MD_SACD_DUMP：把 mpv 实际读到的字节按偏移写进稀疏文件
    FILE* dumpLog = nullptr;
    int area = 0;
    int track = 1;
    qint64 size = 0;
    qint64 pos = 0;
    quint32 nextId = 1;
};

constexpr quint32 kErrorFlag = 0x80000000u;

int64_t streamSize(void* cookie) {
    return static_cast<Stream*>(cookie)->size;
}

int64_t streamSeek(void* cookie, int64_t offset) {
    Stream* s = static_cast<Stream*>(cookie);
    if (offset < 0)
        return MPV_ERROR_GENERIC;
    s->pos = offset > s->size ? s->size : offset;
    return s->pos;
}

int64_t streamRead(void* cookie, char* buf, uint64_t nbytes) {
    Stream* s = static_cast<Stream*>(cookie);
    if (!s->session.alive() || s->pos >= s->size)
        return 0;
    quint64 want = nbytes;
    if (qint64(want) > s->size - s->pos)
        want = quint64(s->size - s->pos);
    if (want == 0)
        return 0;

    const quint32 id = s->nextId++ & 0x7fffffffu;
    const QByteArray req = QStringLiteral("{\"id\":%1,\"cmd\":\"read\",\"area\":%2,\"track\":%3,"
                                          "\"offset\":%4,\"length\":%5}")
                               .arg(id)
                               .arg(s->area)
                               .arg(s->track)
                               .arg(s->pos)
                               .arg(want)
                               .toUtf8();
    if (!s->session.writeLine(req)) {
        qWarning("sacd 流: 写请求失败（helper 已死？）offset=%lld", (long long)s->pos);
        g_streamFailed.store(true);
        return -1;
    }

    char hdr[8];
    if (!s->session.readExact(hdr, 8)) {
        qWarning("sacd 流: 读帧头失败（helper 已死？）offset=%lld", (long long)s->pos);
        g_streamFailed.store(true);
        return -1;
    }
    const quint32 fid = quint32(quint8(hdr[0])) | (quint32(quint8(hdr[1])) << 8) | (quint32(quint8(hdr[2])) << 16) |
                        (quint32(quint8(hdr[3])) << 24);
    const quint32 len = quint32(quint8(hdr[4])) | (quint32(quint8(hdr[5])) << 8) | (quint32(quint8(hdr[6])) << 16) |
                        (quint32(quint8(hdr[7])) << 24);
    if (fid & kErrorFlag) {
        QByteArray drop(int(len), Qt::Uninitialized);
        s->session.readExact(drop.data(), drop.size());
        g_streamFailed.store(true);
        return -1;
    }
    if (len > want)
        return -1; // 不该发生；宁可报错也不越界写 mpv 的缓冲
    if (len && !s->session.readExact(buf, qsizetype(len))) {
        g_streamFailed.store(true);
        return -1;
    }
    if (s->dumpFd >= 0 && len) {
        ::pwrite(s->dumpFd, buf, len, off_t(s->pos));
        if (s->dumpLog)
            fprintf(s->dumpLog, "%lld %u %lld\n", (long long)s->pos, len,
                    (long long)QDateTime::currentMSecsSinceEpoch());
    }
    s->pos += len;
    return len;
}

void streamClose(void* cookie) {
    Stream* s = static_cast<Stream*>(cookie);
    if (s->dumpFd >= 0)
        ::close(s->dumpFd);
    if (s->dumpLog)
        fclose(s->dumpLog);
    delete s;
}

// sacd://<token>/<area>/<track>
int streamOpen(void* /*user_data*/, char* uri, mpv_stream_cb_info* info) {
    QString path = QString::fromUtf8(uri);
    if (path.startsWith(QLatin1String("sacd://")))
        path.remove(0, 7);
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 3)
        return MPV_ERROR_LOADING_FAILED;
    bool ok1 = false, ok2 = false, ok3 = false;
    const quint64 token = parts.at(0).toULongLong(&ok1);
    const int area = parts.at(1).toInt(&ok2);
    const int track = parts.at(2).toInt(&ok3);
    if (!ok1 || !ok2 || !ok3)
        return MPV_ERROR_LOADING_FAILED;

    const QString iso = lookupPath(token);
    if (iso.isEmpty())
        return MPV_ERROR_LOADING_FAILED;

    auto* s = new Stream;
    s->area = area;
    s->track = track;
    if (!s->session.start(helperPath())) {
        delete s;
        return MPV_ERROR_LOADING_FAILED;
    }
    const QByteArray openReq =
        QStringLiteral("{\"id\":1,\"cmd\":\"open\",\"path\":\"%1\"}").arg(QString(iso).replace(u'"', u'\'')).toUtf8();
    if (!s->session.writeLine(openReq) || s->session.readLine().isEmpty()) {
        delete s;
        return MPV_ERROR_LOADING_FAILED;
    }
    const QByteArray statReq =
        QStringLiteral("{\"id\":2,\"cmd\":\"stat\",\"area\":%1,\"track\":%2}").arg(area).arg(track).toUtf8();
    if (!s->session.writeLine(statReq)) {
        delete s;
        return MPV_ERROR_LOADING_FAILED;
    }
    const QByteArray statRes = s->session.readLine();
    // 只需要 dsf_size 一个数，为它拉 QJsonDocument 进解复用线程不值当。
    const int at = statRes.indexOf("\"dsf_size\":");
    if (at < 0) {
        delete s;
        return MPV_ERROR_LOADING_FAILED;
    }
    s->size = QByteArray(statRes).mid(at + 11).split(',').first().split('}').first().trimmed().toLongLong();
    if (s->size <= 0) {
        delete s;
        return MPV_ERROR_LOADING_FAILED;
    }
    s->nextId = 3;

    if (const QByteArray dir = qgetenv("MD_SACD_DUMP"); !dir.isEmpty()) {
        const QString base = QStringLiteral("%1/a%2t%3").arg(QString::fromLocal8Bit(dir)).arg(area).arg(track);
        s->dumpFd = ::open(base.toLocal8Bit().constData() + QByteArray(".bin"), O_CREAT | O_RDWR, 0644);
        s->dumpLog = fopen((base + QStringLiteral(".log")).toLocal8Bit().constData(), "w");
        qInfo("sacd 流: dump → %s.bin (fd=%d)", qUtf8Printable(base), s->dumpFd);
    }
    info->cookie = s;
    info->read_fn = streamRead;
    info->seek_fn = streamSeek;
    info->size_fn = streamSize;
    info->close_fn = streamClose;
    return 0;
}

} // namespace

void registerProtocol(mpv_handle* mpv) {
    if (!mpv)
        return;
    mpv_stream_cb_add_ro(mpv, "sacd", nullptr, streamOpen);
}

bool takeStreamFailure() {
    return g_streamFailed.exchange(false);
}

QString makeUri(const QString& isoPath, int area, int track) {
    QMutexLocker lock(&g_mutex);
    for (auto it = g_registry.cbegin(); it != g_registry.cend(); ++it)
        if (it->isoPath == isoPath)
            return QStringLiteral("sacd://%1/%2/%3").arg(it.key()).arg(area).arg(track);
    const quint64 token = g_nextToken++;
    g_registry.insert(token, Entry{isoPath});
    return QStringLiteral("sacd://%1/%2/%3").arg(token).arg(area).arg(track);
}

} // namespace md::media::sacd
