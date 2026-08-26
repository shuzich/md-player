// 断点续播存储（M1-PLAN T2「简单断点续播」）。
// 退出时把 {文件标识: 位置} 写进应用数据目录的 JSON，重开时询问是否继续。
#pragma once

#include <QHash>
#include <QObject>
#include <QString>

namespace md::core {

struct ResumeEntry {
    double position = 0.0;
    double duration = 0.0;
    qint64 size = 0;    // 文件大小，用于识别同名但内容已换的文件
    qint64 savedAt = 0; // Unix 秒
    bool isValid() const { return position > 0.0; }
};

class ResumeStore : public QObject {
    Q_OBJECT

public:
    explicit ResumeStore(QObject* parent = nullptr);

    // 查询：返回的 entry 仅在文件大小一致时有效，避免同路径换片后跳错位置。
    ResumeEntry lookup(const QString& uri) const;
    void remember(const QString& uri, double position, double duration);
    void forget(const QString& uri);
    void save() const;

    // 距片尾这么近就不记了，避免"看完了还问你要不要续播"。
    static constexpr double kTailGuardSeconds = 15.0;
    // 太靠前也不值得问。
    static constexpr double kHeadGuardSeconds = 30.0;

private:
    static QString storePath();
    static QString keyFor(const QString& uri);
    void load();

    QHash<QString, ResumeEntry> entries_;
};

} // namespace md::core
