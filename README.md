# TouchDesigner to PatternFlow

Hi! This is a **WIP patch** that allows you to stream any TOP from **TouchDesigner** to **PatternFlow**.

It also allows you to stream **encoder values back to TouchDesigner** for visual control.

## Installation

### 1. Download the Patch

Download the patch from the [`td_patternflow` branch on GitHub](https://github.com/zhuo-ran-liu/PatternFlow/tree/td_patternflow).

### 2. Configure PatternFlow

Open:

```text
td_patternflow_ini/src/config.h
```

Set the following values:

* `TD_IP`
* `TD_OSC_PORT`
* `VIDEO_PORT`
* Your Wi-Fi credentials

### 3. Flash the PatternFlow

Flash the PatternFlow with the `.ini` file using **VS Code**.

## TouchDesigner Setup

Once the PatternFlow is configured and flashed, open the TouchDesigner patch.

### `udp_sender` DAT

In the `udp_sender` DAT, set:

```text
ESP_IP = <IP address of your PatternFlow ESP32>
PORT = <VIDEO_PORT>
```

Where `VIDEO_PORT` should match the value you defined in `config.h`.

### `knob`

In the `knob` component, set:

```text
Network Port = <TD_OSC_PORT>
```

This should match the `TD_OSC_PORT` value you defined in `config.h`.

## Configuration Summary

| Setting           | Where to configure        | Description                         |
| ----------------- | ------------------------- | ----------------------------------- |
| `TD_IP`           | `config.h`                | TouchDesigner IP address            |
| `TD_OSC_PORT`     | `config.h` / `knob`       | OSC network port                    |
| `VIDEO_PORT`      | `config.h` / `udp_sender` | Video streaming port                |
| Wi-Fi credentials | `config.h`                | PatternFlow Wi-Fi connection        |
| `ESP_IP`          | `udp_sender`              | IP address of the PatternFlow ESP32 |

## Current Limitations

* ⚠️ Video streaming is currently limited to **64 × 32 pixels**.
* This is a **work in progress (WIP)** patch.
