feat: Add keep_connections feature to maintain subscriber connections during publisher disconnects

## Summary
Implements a new feature that prevents FFmpeg subscribers from disconnecting 
when the publisher experiences temporary network issues or restarts.

## Key Changes
- Added `keep_connections` and `reconnect_timeout` configuration directives
- Modified live module to preserve subscriber connections during publisher disconnect
- Added timer-based mechanism for handling publisher reconnection timeout
- Enhanced command module to ignore disconnect signals from subscribers in keep mode

## Configuration
```nginx
application live {
    live on;
    keep_connections on;           # Enable feature (default: off) 
    reconnect_timeout 30s;         # Publisher reconnect timeout (default: 30s)
}
```

## Behavior
1. **Publisher disconnect**: Subscribers remain connected, timer starts
2. **Publisher reconnect**: Timer canceled, subscribers resume receiving stream  
3. **Timeout exceeded**: All subscribers disconnected, stream reset

## Benefits
- Improved streaming stability for FFmpeg transcoding pipelines
- Reduced downtime during publisher network issues
- Backward compatible (disabled by default)
- Memory safe with proper timer management

## Files Modified
- ngx_rtmp_live_module.h: Add configuration and stream state fields
- ngx_rtmp_live_module.c: Implement core keep connections logic
- ngx_rtmp_cmd_module.c: Modify disconnect handling for subscribers

## Files Added  
- KEEP_CONNECTIONS.md: Feature documentation
- nginx.conf.example: Configuration examples
- CHANGES.md: Detailed change summary

Resolves issue with FFmpeg subscribers disconnecting on publisher network issues.
