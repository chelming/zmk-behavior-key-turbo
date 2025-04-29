# ZMK Key Turbo Behavior Module

A ZMK behavior that provides auto-repeat functionality for keys with configurable timing.

## Overview

The Key Turbo behavior allows you to:

1. Press a key normally for a single press
2. Hold a key longer than a defined tapping term to trigger auto-repeat with custom timing
3. Release the key to stop the auto-repeat sequence

## How It Works

- When you press a key with this behavior:
  - Initial keypress is sent immediately
  - If held longer than `tapping-term-ms`, a turbo sequence starts
  - While in the turbo sequence, it repeatedly presses/releases the key using `pause-ms` and `press-ms` timing
  - The turbo stops when you release the key

## Parameters

| Parameter | Description |
|-----------|-------------|
| `tapping-term-ms` | Delay before turbo starts (milliseconds) |
| `pause-ms` | Time to wait between key presses (milliseconds) |
| `press-ms` | How long the key should be held down during each cycle |
| `bindings` | The behavior to use for key press/release (usually &kp) |

## Installation

Add this repository as a ZMK module by adding it to your `west.yml`:

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: chelming
      url-base: https://github.com/chelming
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-behavior-key-turbo
      remote: chelming
      revision: main
```

## Usage

### 1. Define the behavior in your `.keymap` file:

```dts
/ {
    behaviors {
        // Regular keyboard key turbo
        kt: key_turbo {
            compatible = "zmk,behavior-key-turbo";
            #binding-cells = <1>;
            bindings = <&kp>;           // Use kp behavior for key press/release
            tapping-term-ms = <300>;    // Start turbo after holding 300ms
            pause-ms = <25>;            // Wait 25ms between key presses
            press-ms = <25>;            // Key is down for 25ms of each cycle
        };
        
        // Media control turbo
        mt: media_turbo {
            compatible = "zmk,behavior-key-turbo";
            #binding-cells = <1>;
            bindings = <&kp>;           // Use kp behavior for media keys too
            tapping-term-ms = <300>;    // Start turbo after holding 300ms
            pause-ms = <50>;            // Longer pause for media controls
            press-ms = <50>;            // Longer press for media controls
        };
    };

    keymap {
        compatible = "zmk,keymap";
        default_layer {
            bindings = <
                // Use on keyboard keys
                &kt KP_N1    &kt KP_N2    &kt KP_N3
                
                // Use on arrow keys
                &kt UP       &kt LEFT     &kt RIGHT    &kt DOWN
                
                // Use on media keys
                &mt C_VOL_UP &mt C_VOL_DN &mt C_BRI_UP &mt C_BRI_DN
            >;
        };
    };
};
```

### 2. Example Usage Scenarios:

- **Fast Scrolling**: Use turbo on arrow keys for quick navigation in documents
- **Gaming**: For repeating actions quickly in games
- **Volume Control**: Quick volume adjustment with media turbo
- **Number Entry**: Fast number input with numeric keypad

The key will send a single press when tapped, but when held will eventually enter turbo mode and repeat at the configured rate.
