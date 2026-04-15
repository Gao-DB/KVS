# KVS

## Device-side KVS producer uploader

- Build target: `kvs_event_uploader` from `/home/runner/work/KVS/KVS/kvs_server/CMakeLists.txt`.
- SDK location: `/home/runner/work/KVS/KVS/kvs_open-source` (full `amazon-kinesis-video-streams-producer-c` source is expected there).
- The worker thread creates a new stream per event with name format `event-<filename>-<seq>-<timestamp>` and uploads ~12 seconds of audio/video from ring buffer APIs.

### Environment variables

- `AWS_REGION` (optional, defaults to `us-east-1`)
- `AWS_ACCESS_KEY_ID` and `AWS_SECRET_ACCESS_KEY` (required unless temporary credential backend flow is implemented)
- `AWS_SESSION_TOKEN` (optional)
- `KVS_CA_CERT_PATH` (optional)
- `KVS_CLIENT_CERT_PATH` / `KVS_CLIENT_KEY_PATH` (optional placeholders for custom callback-provider flow)
- `KVS_TEMP_CREDENTIAL_ENDPOINT` (optional placeholder endpoint for fetching short-term credentials from backend)
