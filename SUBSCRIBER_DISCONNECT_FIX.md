# Giải quyết vấn đề Subscriber bị ngắt kết nối sau vài tiếng

## Nguyên nhân và giải pháp

### Các nguyên nhân chính:

1. **Idle Timer Cleanup không đúng** - Timer không được cleanup khi session disconnect
2. **Memory corruption** - Timer event trỏ đến session đã bị giải phóng  
3. **Nginx connection timeout** - Timeout mặc định của nginx
4. **RTMP connection timeout** - Timeout trong RTMP protocol

### Các sửa đổi đã thực hiện:

#### 1. Sửa ngx_rtmp_live_idle() - Thêm kiểm tra publisher
```c
static void
ngx_rtmp_live_idle(ngx_event_t *pev)
{
    // ... kiểm tra null pointers ...
    
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_live_module);
    if (ctx == NULL || !ctx->publishing) {
        /* Chỉ drop idle publishers, không phải subscribers */
        return;
    }
    
    // Drop publisher
    ngx_rtmp_finalize_session(s);
}
```

#### 2. Cleanup idle timer trong ngx_rtmp_live_close_stream()
```c
if (ctx->publishing) {
    /* Cancel idle timer if it's set */
    if (ctx->idle_evt.timer_set) {
        ngx_del_timer(&ctx->idle_evt);
    }
}
```

#### 3. Cleanup idle timer trong ngx_rtmp_live_disconnect()
```c
/* Clean up idle timer for any session */
if (ctx->idle_evt.timer_set) {
    ngx_del_timer(&ctx->idle_evt);
}
```

### Cấu hình Nginx để tránh timeout:

```nginx
rtmp {
    server {
        listen 1935;
        
        # Tăng timeout cho connection
        timeout 60s;           # Mặc định 60s - có thể tăng lên
        ping 30s;              # Ping interval
        ping_timeout 30s;      # Ping timeout
        
        application live {
            live on;
            
            # TẮT idle timeout cho publisher (để subscriber không bị ảnh hưởng)
            drop_idle_publisher off;
            
            # Hoặc set thời gian dài hơn (24 giờ = 86400000ms)
            # drop_idle_publisher 86400000;
            
            # Keep connections settings
            keep_connections on;
            reconnect_timeout 30s;
            
            # Buffer settings để tránh sync issues
            sync 300ms;
            buffer 0;
            
            # Các settings khác
            wait_key on;
            wait_video off;
            interleave off;
            
            allow publish all;
            allow play all;
        }
    }
}

# Main nginx settings
events {
    worker_connections 2048;
}

http {
    # Tăng timeout cho HTTP nếu cần
    keepalive_timeout 300s;
    client_body_timeout 300s;
    client_header_timeout 300s;
    send_timeout 300s;
}
```

### Kiểm tra và debug:

#### 1. Kiểm tra logs
```bash
# Theo dõi error log
tail -f /var/log/nginx/error.log | grep -E "(idle|timeout|drop|finalize)"

# Theo dõi RTMP access log  
tail -f /var/log/nginx/rtmp_access.log
```

#### 2. Test script để kiểm tra connection stability
```bash
#!/bin/bash
# test_subscriber_stability.sh

# Tạo nhiều subscriber và theo dõi
for i in {1..10}; do
    ffplay -i rtmp://localhost/live/test &
    echo "Started subscriber $i"
done

# Theo dõi trong 2 giờ
sleep 7200

# Kill all subscribers
pkill ffplay
```

#### 3. Monitor connection status
```bash
# Kiểm tra active connections
netstat -an | grep :1935 | wc -l

# Kiểm tra nginx processes
ps aux | grep nginx

# Kiểm tra memory usage
free -h
```

### Các cấu hình tối ưu khuyến nghị:

#### Cho production environment:
```nginx
rtmp {
    server {
        listen 1935;
        timeout 300s;        # 5 phút timeout
        ping 60s;            # Ping mỗi 1 phút  
        ping_timeout 30s;    # Ping timeout 30s
        
        application live {
            live on;
            
            # TẮT hoàn toàn idle timeout
            drop_idle_publisher off;
            
            # Keep connections với timeout dài
            keep_connections on;
            reconnect_timeout 60s;
            
            # Tối ưu buffer
            sync 500ms;
            buffer 5s;          # Buffer 5 giây
            
            # Stream settings
            wait_key on;
            wait_video off;
            interleave on;      # Cải thiện sync
            
            # Allow settings  
            allow publish 192.168.0.0/16;  # Chỉ allow publish từ LAN
            allow play all;
        }
    }
}
```

#### Cho development/testing:
```nginx
rtmp {
    server {
        listen 1935;
        timeout 3600s;       # 1 giờ timeout
        ping 300s;           # Ping mỗi 5 phút
        ping_timeout 60s;    # Ping timeout 1 phút
        
        application live {
            live on;
            
            # Idle timeout rất dài (24 giờ)
            drop_idle_publisher 86400000;
            
            keep_connections on; 
            reconnect_timeout 120s;
            
            allow publish all;
            allow play all;
        }
    }
}
```

### Troubleshooting checklist:

1. ✅ **Kiểm tra idle timer cleanup** - Đã sửa trong code
2. ✅ **Thêm null pointer checks** - Đã sửa trong ngx_rtmp_live_idle()  
3. ✅ **Cleanup timer trong disconnect** - Đã thêm
4. ⚠️ **Kiểm tra nginx timeout settings** - Cần cấu hình
5. ⚠️ **Kiểm tra system limits** - ulimit, file descriptors
6. ⚠️ **Monitor memory usage** - Tránh memory leak

### Testing:

Sau khi áp dụng các sửa đổi:

1. **Recompile nginx với module đã sửa**
2. **Áp dụng cấu hình mới**  
3. **Test với multiple subscribers trong thời gian dài**
4. **Monitor logs và resource usage**

Các sửa đổi này sẽ giải quyết vấn đề subscriber bị ngắt kết nối sau vài tiếng do timer cleanup không đúng cách.
