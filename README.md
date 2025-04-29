# ZMK Key Turbo Behavior Module

A ZMK behavior that provides auto-repeat functionality for keys with configurable timing.

## Overview

The Key Turbo behavior allows you to:

1. Press a key normally for a single press
2. Hold a key longer than a defined delay to trigger auto-repeat with custom timing
3. Release the key to stop the auto-repeat sequence

## How It Works

- When you press a key with this behavior:
  - Initial keypress is sent immediately
  - If held longer than `delay-ms`, a turbo sequence starts
  - While in the turbo sequence, it repeatedly presses/releases the key using `tempo-ms` and `hold-ms` timing
  - The turbo stops when you release the key

## Parameters

| Parameter | Description |
|-----------|-------------|
| `delay-ms` | Delay before turbo starts (milliseconds) |
| `tempo-ms` | Overall cycle length (milliseconds) |
| `hold-ms` | How long each key is held down (milliseconds) |

## Installation

Add this repository as a ZMK module by adding it to your `west.yml`:

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: key-turbo
      url-base: https://github.com/chelming
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-behavior-key-turbo
      remote: key-turbo
      revision: main
```
