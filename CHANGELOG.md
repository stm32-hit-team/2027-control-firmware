# Changelog

## 1.0.0 - 2026-07-23

- Rewrote the old RFID firmware as a PlatformIO STM32Cube project.
- Added a bounded, checksum-validating, escaped-frame RFID parser.
- Added non-blocking polling, multi-block reads, UID-based field presence, and
  removal re-arming.
- Added explicit-length GB2312 TTS queuing and startup speed configuration.
- Added queue-full backpressure so a detected tag is not committed until TTS
  accepts its text, plus exact response-length validation.
- Added macOS host tests with sanitizers and C6/C8 build environments.
