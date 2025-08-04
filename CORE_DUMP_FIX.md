# Sửa lỗi Core Dump trong nginx-rtmp-module

## Tóm tắt các vấn đề đã được sửa

Đã xác định và sửa các vấn đề có thể gây ra core dump ảnh hưởng đến tất cả các stream trong nginx-rtmp-module:

### 1. Null Pointer Dereference trong `ngx_rtmp_live_stop()`
**Vấn đề:** Hàm không kiểm tra `ctx` có null hay không trước khi truy cập `ctx->publishing`
**Sửa:** Thêm kiểm tra `ctx &&` trước khi sử dụng ctx

### 2. Null Pointer trong `ngx_rtmp_live_restart_subscribers()`
**Vấn đề:** Không kiểm tra `pctx->session` và `ss->connection` trước khi sử dụng
**Sửa:** Thêm kiểm tra `pctx->session` và `ss->connection` trước khi truy cập

### 3. Null Pointer trong `ngx_rtmp_live_reconnect_timeout()`
**Vấn đề:** Tương tự như vấn đề 2, không kiểm tra session và connection
**Sửa:** Thêm kiểm tra null pointer trước khi gọi `ngx_rtmp_finalize_session()`

### 4. Race Condition trong `ngx_rtmp_live_set_status()`
**Vấn đề:** Truy cập `pctx->session->connection` mà không kiểm tra null
**Sửa:** Thêm kiểm tra `pctx->session` và `pctx->session->connection`

### 5. Buffer Overflow Prevention trong `ngx_rtmp_live_get_stream()`
**Vấn đề:** Không kiểm tra độ dài tên stream có thể gây overflow
**Sửa:** Thêm kiểm tra `name != NULL` và giới hạn độ dài tên stream

### 6. Session Validation trong `ngx_rtmp_live_av()`
**Vấn đề:** Broadcasting đến subscriber mà không kiểm tra session validity
**Sửa:** Thêm kiểm tra `pctx->session` và `ss->connection` trong vòng lặp broadcast

### 7. Connection Check trong `ngx_rtmp_live_disconnect()`
**Vấn đề:** Truy cập `s->connection` mà không kiểm tra null
**Sửa:** Thêm kiểm tra `s->connection` trước khi sử dụng

## Nguyên nhân gây Core Dump

1. **Null Pointer Dereference:** Khi session hoặc connection bị null mà code vẫn cố truy cập
2. **Race Conditions:** Khi một thread đang xử lý session trong khi thread khác đã giải phóng nó
3. **Memory Corruption:** Khi truy cập vùng nhớ đã được giải phóng
4. **Buffer Overflow:** Khi tên stream quá dài gây overflow buffer

## Tại sao ảnh hưởng tất cả streams

Core dump xảy ra ở process level, khi nginx worker process bị crash:
- Tất cả streams đang được xử lý bởi worker đó sẽ bị mất
- Clients sẽ bị disconnect đột ngột
- Nginx sẽ restart worker process mới

## Kiểm tra sau khi sửa

Để đảm bảo các sửa đổi hoạt động đúng:

1. **Compile lại module:**
```bash
make clean
make
```

2. **Test với nhiều concurrent connections:**
```bash
# Sử dụng tools như ffmpeg để tạo nhiều publisher/subscriber đồng thời
# Và disconnect ngẫu nhiên để test race conditions
```

3. **Monitor logs:**
```bash
tail -f /var/log/nginx/error.log
# Kiểm tra không còn segmentation fault errors
```

4. **Stress test:**
- Tạo nhiều streams đồng thời
- Ngắt kết nối publisher đột ngột
- Test với reconnect scenarios

## Các cải tiến khác nên xem xét

1. **Memory Pool Management:** Cải tiến quản lý memory pool để tránh memory leak
2. **Reference Counting:** Implement reference counting cho sessions để tránh use-after-free
3. **Lock Mechanisms:** Thêm locks thích hợp để tránh race conditions
4. **Error Handling:** Cải tiến error handling và recovery mechanisms

Các sửa đổi này sẽ giúp ngăn chặn hầu hết các trường hợp core dump phổ biến trong nginx-rtmp-module.
