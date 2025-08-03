# Keep Connections Feature

Tính năng này cho phép giữ kết nối với subscriber khi publisher bị ngắt kết nối do sự cố mạng, thay vì đóng tất cả kết nối ngay lập tức.

## Cấu hình

Để bật tính năng này, thêm các directive sau vào block `application` trong nginx.conf:

```nginx
rtmp {
    server {
        listen 1935;
        application live {
            live on;
            
            # Bật tính năng giữ kết nối subscriber
            keep_connections on;
            
            # Thời gian chờ publisher kết nối lại (mặc định: 30s, có thể dùng s/ms)
            reconnect_timeout 30s;
            
            # Các cấu hình khác...
        }
    }
}
```

## Cách hoạt động

1. **Publisher disconnect**: Khi publisher ngắt kết nối (do sự cố mạng hoặc đóng ứng dụng), module sẽ:
   - Không gửi EOF signal cho subscribers
   - Không đóng kết nối với subscribers  
   - Bắt đầu đếm thời gian `reconnect_timeout`

2. **Subscriber behavior**: Subscribers sẽ:
   - Không nhận được EOF signal
   - Tiếp tục duy trì kết nối RTMP
   - Chờ publisher kết nối lại

3. **Publisher reconnect**: Khi publisher kết nối lại:
   - Timer `reconnect_timeout` bị hủy
   - Subscribers sẽ nhận được stream mới từ publisher
   - Luồng dữ liệu tiếp tục bình thường

4. **Timeout**: Nếu publisher không kết nối lại trong thời gian `reconnect_timeout`:
   - Tất cả subscribers sẽ bị đóng kết nối
   - Stream sẽ về trạng thái ban đầu

## Lợi ích

- **Tăng độ ổn định**: FFmpeg subscribers không bị ngắt kết nối khi có sự cố mạng tạm thời
- **Giảm thời gian downtime**: Khi publisher kết nối lại, subscribers có thể tiếp tục ngay mà không cần reconnect
- **Tối ưu cho streaming**: Đặc biệt hữu ích cho các ứng dụng streaming yêu cầu độ ổn định cao

## Lưu ý

- Tính năng này chỉ hoạt động với live streaming
- Thời gian `reconnect_timeout` nên được cấu hình phù hợp với yêu cầu ứng dụng
- Nếu `keep_connections` được tắt (off), module sẽ hoạt động như trước đây (đóng tất cả subscribers khi publisher disconnect)

## Tương thích

Tính năng này tương thích với:
- FFmpeg subscribers
- Các RTMP clients khác
- Existing nginx-rtmp-module configurations

## Testing

Để test tính năng này:

1. **Setup publisher**: 
   ```bash
   ffmpeg -f lavfi -i testsrc -c:v libx264 -f flv rtmp://localhost/live/test
   ```

2. **Setup subscriber**:
   ```bash
   ffmpeg -i rtmp://localhost/live/test -c copy output.flv
   ```

3. **Test disconnect**: Dừng publisher (Ctrl+C) và quan sát subscriber không bị ngắt kết nối

4. **Test reconnect**: Khởi động lại publisher với cùng stream name và quan sát subscriber tiếp tục nhận dữ liệu

## Debug

Để debug, bật nginx error log level:
```nginx
error_log /var/log/nginx/error.log debug;
```

Tìm các log messages:
- `live: publisher disconnected, keeping subscribers`
- `live: publisher reconnected, canceling reconnect timeout`
- `live: reconnect timeout for stream`
