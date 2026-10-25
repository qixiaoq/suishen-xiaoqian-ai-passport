<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Suishen Xiaoqian · Pocket Fortune Slips

An independent AI Passport experience built on [FoloToy's open-source firmware](https://github.com/FoloToy/ai-passport). Its four entries are **Today's Slip**, **A Thought Becomes a Hexagram**, **Six Casts for a Question**, and **Birth Chart Note**. The device handles three-button navigation, casting, calendar information, and QR display; a phone companion handles longer questions and plain-language AI interpretation.

This repository publishes **source code for self-hosting and development**. It is not yet an installable AI Passport community play. The companion URL is deliberately blank in the public firmware defaults, so QR interpretation requires your own reachable server and compatible AI model configuration. No API key is included. See [companion setup](companion/README.md) and the [release checkpoint](docs/development/release/suishen-xiaoqian-publish.md).

The firmware targets ESP32-C3 AI Passport hardware with ESP-IDF 5.5.3. The host companion uses Node.js. To validate, run `./tools/validate.sh` in a configured ESP-IDF environment. A merged image can be built for standalone flashing; the application image is only a **candidate** for a MetaPass2 child slot and has not been tested there. Do not use this source as a ready-to-install community package.

The original project license is [MIT](LICENSE). The renamed subset of Adobe Source Han Serif uses the [SIL Open Font License](assets/fonts/LICENSE.txt); the vendored QR encoder retains its MIT notice in [its source](main/vendor/qrcodegen/qrcodegen.h).
