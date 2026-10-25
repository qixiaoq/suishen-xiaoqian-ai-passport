<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# 随身小签 · 掌中签馆

基于 [FoloToy 开源固件](https://github.com/FoloToy/ai-passport)制作的独立 AI Passport 玩法，设有**今日一签、一念成卦、六掷问事、生辰小笺**四个入口。设备负责三键操作、起卦、黄历信息和二维码；手机伴侣负责输入较长的问题，并通过 AI 提供白话解读。

本仓库先公开**供自行部署和开发的源码**，尚未发布可直接安装的 AI Passport 社区玩法。公开版固件默认不填写手机解读服务地址；要使用扫码解读，需自行配置手机可访问的服务及兼容的 AI 模型。仓库不包含 API Key。请参阅[手机伴侣部署说明](companion/README.zh_CN.md)和[发布准备记录](docs/development/release/suishen-xiaoqian-publish.zh_CN.md)。

固件面向 ESP32-C3 AI Passport，使用 ESP-IDF 5.5.3；手机伴侣使用 Node.js。在配置好 ESP-IDF 的环境中运行 `./tools/validate.sh` 可完成验证。项目可生成从 `0x0` 烧录的单机合并镜像；应用镜像目前只是 MetaPass2 子槽**候选**，尚未经过真机安装测试，不应视作可直接安装的社区包。

原项目采用 [MIT 许可证](LICENSE)。附带的 Adobe 思源宋体重新命名子集遵循 [SIL 开放字体许可证](assets/fonts/LICENSE.txt)；内置的二维码编码器在[源码](main/vendor/qrcodegen/qrcodegen.h)中保留 MIT 许可声明。
