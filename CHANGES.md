# Summary of Changes - Keep Connections Feature

## Files Modified

### 1. ngx_rtmp_live_module.h
- Added `keep_connections` flag to configuration
- Added `reconnect_timeout` to configuration  
- Added `publisher_disconnected`, `keep_subscribers`, `reconnect_evt` to stream structure

### 2. ngx_rtmp_live_module.c
- Added new configuration directives: `keep_connections`, `reconnect_timeout`
- Implemented logic to keep subscriber connections when publisher disconnects
- Added timer-based reconnect timeout mechanism
- Modified `ngx_rtmp_live_set_status()` to skip EOF for subscribers in keep mode
- Modified `ngx_rtmp_live_stop()` to avoid sending stream EOF to subscribers
- Added `ngx_rtmp_live_disconnect()` handler for unexpected disconnects
- Added `ngx_rtmp_live_restart_subscribers()` to restart subscribers on publisher reconnect
- Added proper timer cleanup in stream deletion

### 3. ngx_rtmp_cmd_module.c
- Modified `ngx_rtmp_cmd_close_stream_init()` to ignore closeStream from subscribers in keep mode
- Modified `ngx_rtmp_cmd_delete_stream()` to ignore deleteStream from subscribers in keep mode

### 4. New Files Created
- `KEEP_CONNECTIONS.md` - Documentation for the new feature
- `nginx.conf.example` - Example configuration file

## How It Works

1. **Publisher Disconnect**: When publisher disconnects (network issues or intentional), the module:
   - Sets `publisher_disconnected` and `keep_subscribers` flags
   - Starts `reconnect_timeout` timer
   - Does NOT send EOF to subscribers
   - Does NOT close subscriber connections

2. **Subscriber Persistence**: FFmpeg and other RTMP subscribers:
   - Don't receive EOF signal
   - Maintain RTMP connection
   - Wait for publisher to return

3. **Publisher Reconnect**: When publisher reconnects:
   - Cancels reconnect timeout timer
   - Resets disconnection flags
   - Restarts all subscribers to receive new stream

4. **Timeout Handling**: If publisher doesn't reconnect within timeout:
   - Closes all subscriber connections
   - Resets stream to initial state

## Configuration

```nginx
application live {
    live on;
    keep_connections on;           # Enable feature (default: off)
    reconnect_timeout 30s;         # Wait time (default: 30s)
}
```

## Benefits

- Prevents FFmpeg subscribers from disconnecting on temporary network issues
- Reduces downtime when publisher reconnects
- Improves streaming stability for production environments
- Maintains backward compatibility (disabled by default)

## Memory Safety

- Proper timer management to prevent memory leaks
- Safe iteration over subscriber list with next_pctx
- Timer cleanup on stream deletion
- Protection against use-after-free scenarios
