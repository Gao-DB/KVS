# kvs_server device uploader

## Overview
`kvs_server` implements device-side event upload logic for Amazon Kinesis Video Streams:
- `kvs_service_proc` waits for event triggers.
- Each event creates a new stream name and uploads ringbuf audio/video for **12 seconds**.
- Created streams are retained (no immediate delete).

## Build
```bash
cd /home/runner/work/KVS/KVS
cmake -S kvs_server -B build/kvs_server
cmake --build build/kvs_server --target kvs_event_uploader
```

## SDK dependency
This uploader is designed to integrate with the full
`amazon-kinesis-video-streams-producer-c` source under:

`/home/runner/work/KVS/KVS/kvs_open-source`

If the full SDK is not present, keep this path as a submodule/reference and populate it with upstream sources without modifying SDK code.

## Runtime credentials
For local testing, environment credentials are read directly:
- `AWS_REGION` (or `AWS_DEFAULT_REGION`)
- `AWS_ACCESS_KEY_ID`
- `AWS_SECRET_ACCESS_KEY`
- `AWS_SESSION_TOKEN` (optional)

### Short-term credential (STS AssumeRole) placeholder flow
The code includes a backend credential refresh placeholder via:
- `KVS_STS_ENDPOINT`
- `KVS_ROLE_ARN`

Expected production flow:
1. Device calls backend `KVS_STS_ENDPOINT` with device identity.
2. Backend performs AssumeRole and returns temporary credentials.
3. Device refreshes in-memory credentials used by uploader.

## Run (simulated trigger)
```bash
/home/runner/work/KVS/KVS/build/kvs_server/kvs_event_uploader [event_count] [trigger_interval_sec]
```

Example:
```bash
/home/runner/work/KVS/KVS/build/kvs_server/kvs_event_uploader 2 1
```
