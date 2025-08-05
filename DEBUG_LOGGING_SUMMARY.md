# Debug Logging Enhancements for nginx-rtmp-module

## Overview
This document summarizes all debug logging enhancements added to help track subscriber disconnection issues after several hours of streaming.

## Problem Statement
- Subscribers are being disconnected after several hours of streaming
- Core dumps affecting all streams  
- Need extensive logging to understand what happens at each step

## Files Modified
- `ngx_rtmp_live_module.c` - Main live streaming module

## Logging Enhancements Added

### 1. Timer Management Functions

#### ngx_rtmp_live_idle()
- Added session pointer logging
- Added timer validation checks
- Added idle timeout information
- Added publisher disconnection logging

#### ngx_rtmp_live_reconnect_timeout()
- Added reconnection attempt logging
- Added session and timer state tracking
- Added error logging for failed reconnections

### 2. Session Management Functions

#### ngx_rtmp_live_join()
- Added comprehensive session joining logs
- Added stream creation and subscription tracking
- Added subscriber count logging
- Added error conditions logging

#### ngx_rtmp_live_disconnect()
- Added session disconnection tracking
- Added timer cleanup logging
- Added publisher/subscriber differentiation
- Added connection validation checks

#### ngx_rtmp_live_close_stream()
- Added stream closure tracking
- Added subscriber cleanup logging
- Added timer cleanup verification
- Added Play.Stop status logging

### 3. Status and State Management

#### ngx_rtmp_live_set_status()
- Added status change tracking
- Added active/inactive state transitions
- Added stream start/stop logging

### 4. Packet Broadcasting (ngx_rtmp_live_av)
- Added packet processing entry/exit logging
- Added subscriber enumeration tracking
- Added packet send success/failure logging
- Added dropped packet tracking
- Added mandatory packet failure logging
- Added bandwidth and peer counting

### 5. Subscriber Management

#### ngx_rtmp_live_play()
- Added play request tracking
- Added subscriber join logging
- Added context validation
- Added status notification logging

#### ngx_rtmp_live_pause()
- Added pause/unpause request tracking
- Added stream state changes
- Added error condition logging

## Log Levels Used

### NGX_LOG_INFO
- Used for important operational events
- Session lifecycle events
- Timer operations
- Connection/disconnection events
- Packet broadcasting events

### NGX_LOG_ERR  
- Used for error conditions
- Failed operations
- Missing contexts or configurations
- Send failures

### NGX_LOG_DEBUG_RTMP
- Maintained existing debug logs
- Added additional debug context where needed

## Key Monitoring Points

### For Subscriber Disconnections
1. **Timer Events**: Monitor idle timer and reconnect timer logs
2. **Session Lifecycle**: Track join, disconnect, and close_stream events
3. **Packet Broadcasting**: Monitor send failures and dropped packets  
4. **Memory Management**: Check for null pointer access
5. **Connection Validation**: Verify session->connection validity

### Critical Log Messages to Watch
- `"live: idle timer fired"` - Indicates idle timeout
- `"live: av send failed"` - Packet send failures  
- `"live: mandatory packet failed, finalizing session"` - Forced disconnections
- `"live: disconnect - cleaning up timers"` - Timer cleanup
- `"live: session has no connection"` - Connection issues

## Usage Instructions

1. Set nginx error log level to `info` or higher to see the enhanced logs
2. Monitor logs during long streaming sessions
3. Look for patterns before subscriber disconnections
4. Pay attention to timer events and send failures
5. Check for memory/connection validation errors

## Configuration Example
```nginx
error_log /var/log/nginx/error.log info;

rtmp {
    server {
        listen 1935;
        
        application live {
            live on;
            # Enable debug logging
            access_log /var/log/nginx/rtmp_access.log;
        }
    }
}
```

## Expected Log Flow for Normal Operation

### Publisher Connection:
1. `live: join request for session`
2. `live: created new stream`  
3. `live: set_status - marking stream as active`

### Subscriber Connection:
1. `live: play request for session`
2. `live: play joining stream as subscriber`
3. `live: join - added subscriber to existing stream`

### Packet Broadcasting:
1. `live: av handler called for session`
2. `live: av starting broadcast to subscribers`
3. `live: av processing subscriber session`
4. `live: av sending relative packet to subscriber`
5. `live: av packet sent successfully to subscriber`

### Timer Events:
1. `live: idle timer fired for session` (every idle_timeout period)
2. `live: idle timer - session still has active stream` (if still publishing)

## Troubleshooting Guide

### If Subscribers Disconnect After Hours:
1. Check for `idle timer fired` messages
2. Look for `av send failed` patterns
3. Monitor `dropped packet` counts
4. Verify timer cleanup in disconnect events
5. Check for connection validation failures

### If Core Dumps Occur:
1. Look for null pointer access logs
2. Check timer cleanup sequences
3. Monitor session lifecycle events
4. Verify proper context management

This logging framework should provide comprehensive visibility into the nginx-rtmp-module's operation and help identify the root cause of subscriber disconnection issues.
