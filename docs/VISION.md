# 产品愿景

> **非当期承诺；当期以 [M1-PLAN.md](M1-PLAN.md) 为准。**
> 本文描述方向与终局形态，不构成任何里程碑的交付范围。

## 定位

播放器 + 本地边缘采集节点。

让播放器知道自己在播哪张 Release、哪个 Medium、哪条 Track，而不是只知道 `00001.m2ts`。

## 双模式

### Player（收藏用户）

打开即识别、正确播放、曲目 / 章节 / 音轨 / 字幕、虚拟菜单、收藏信息。

### Studio / Publisher（研究者与发布组）

- MediaInfo / BDInfo 式技术参数
- 批量截图、九宫格
- Playlist / Chapter 与流分析
- 发布资料生成（PT-Gen 复用 MusicDisc 平台能力，**不本地重写**）
- 图床上传

## 边缘采集管线

```
本地观察（结构 / 流 / 指纹）
        ↓
     Observed
        ↓
   MusicDisc API
        ↓
 Research / Resolve
        ↓
     人工审核
        ↓
      入库
```

**机器负责观察，AI 负责研究，人负责批准；播放器绝不直写数据库。**

## 组件候选（Studio 期引入）

libmediainfo、ffprobe、成熟图像组件（九宫格 / 缩略图）。

## 里程碑映射

| 阶段 | 内容 |
|---|---|
| M1 | 播放基座 |
| M2 / P2 | 虚拟菜单 + 只读匹配 |
| M3 | Studio 工作台 |
| P3 / P4 | 观察数据上行（需 MusicDisc 后端配套，届时另立项） |
