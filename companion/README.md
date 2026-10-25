<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Companion service

The device's `192.168.4.1` hotspot page only provisions Wi-Fi. It is not the interpretation service. After setup, the phone returns to its normal network; interpretation QR links still need this reachable companion service.

This service receives a compact QR link from the device, opens a temporary session, and sends the visitor's question to an OpenAI-compatible chat completion API. Sessions live in memory for 30 minutes. The visitor can save a result in the current browser explicitly.

## Run locally

```sh
cd companion
npm ci
AI_API_BASE_URL=https://api.example.com/v1/ AI_API_KEY=your-key AI_MODEL=your-model npm start
```

The default port is `8787`. For a same-Wi-Fi hardware test, set `APP_BASE_URL` and firmware `CONFIG_XIAOQIAN_COMPANION_URL` to the computer's LAN address (for example `http://192.168.1.10:8787`) and keep the service running. A phone cannot use the computer's `localhost`. For friends on other networks, deploy the service at a public HTTPS address and use that address in both settings (without a trailing slash in firmware).

The public source defaults `CONFIG_XIAOQIAN_COMPANION_URL` to empty; it does not contain the developer's LAN address. Set a real service URL before building a personal image. Without one, the device explicitly says to configure the phone interpretation service. This source change does not alter firmware already flashed to a device. The local `/setup` flow is for development testing and loses its key on service restart. Before offering a public service, design credential handling, cost limits, and access control; never embed a personal API key in firmware, web assets, or the repository.

For a local test, you may also start with `npm start` and open `http://127.0.0.1:8787/setup` **on the computer running the service**. Enter a compatible HTTPS Chat Completions base URL, model name, and API key there. The key remains in process memory and is not written to the firmware or project files; repeat setup after restarting the service. Refresh the scanned phone page to enable interpretation. Without a model configuration, the phone page now explains why interpretation is unavailable.

A Dockerfile is included for container deployment. Supply the three AI environment variables and `APP_BASE_URL` through the host's secret/configuration settings; never bake the API key into an image.

`DEVICE_TOKEN` is only needed by the optional authenticated `POST /api/sessions` endpoint. The device's compact `/q/` QR route does not need a token or direct HTTP connection to this service. Apply a deployment proxy with request limits before public exposure because QR sessions are created without an account.

The calendar and birth-chart calculations use `lunar-javascript` 1.7.7 under its MIT license. The same software calculates the daily almanac values and the lunar date used for time casting. The displayed cultural interpretation is not a factual prediction.

## Checks

```sh
npm test
```
