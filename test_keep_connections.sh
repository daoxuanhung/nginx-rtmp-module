#!/bin/bash

# Test script cho keep_connections feature
echo "Testing keep_connections feature..."

# Kiểm tra xem nginx có đang chạy không
if ! pgrep nginx > /dev/null; then
    echo "Error: nginx is not running"
    exit 1
fi

# Kiểm tra cấu hình
echo "Checking nginx configuration..."
nginx -t
if [ $? -ne 0 ]; then
    echo "Error: nginx configuration is invalid"
    exit 1
fi

echo "Starting test..."

# Bắt đầu publisher (chạy trong background)
echo "Starting publisher..."
ffmpeg -f lavfi -i testsrc -c:v libx264 -preset ultrafast -tune zerolatency -f flv rtmp://localhost/live/test &
PUBLISHER_PID=$!

sleep 3

# Bắt đầu subscriber (chạy trong background)
echo "Starting subscriber..."
ffmpeg -i rtmp://localhost/live/test -c copy -f null - &
SUBSCRIBER_PID=$!

sleep 3

# Kiểm tra cả hai đã chạy
if ! kill -0 $PUBLISHER_PID 2>/dev/null; then
    echo "Error: Publisher failed to start"
    exit 1
fi

if ! kill -0 $SUBSCRIBER_PID 2>/dev/null; then
    echo "Error: Subscriber failed to start"
    exit 1
fi

echo "Both publisher and subscriber are running"

# Dừng publisher để test keep_connections
echo "Stopping publisher to test keep_connections..."
kill $PUBLISHER_PID
wait $PUBLISHER_PID 2>/dev/null

sleep 2

# Kiểm tra subscriber vẫn chạy
if kill -0 $SUBSCRIBER_PID 2>/dev/null; then
    echo "SUCCESS: Subscriber is still running after publisher disconnect"
else
    echo "FAILED: Subscriber disconnected when publisher stopped"
    exit 1
fi

# Chờ một chút rồi restart publisher
sleep 5

echo "Restarting publisher..."
ffmpeg -f lavfi -i testsrc -c:v libx264 -preset ultrafast -tune zerolatency -f flv rtmp://localhost/live/test &
PUBLISHER_PID=$!

sleep 3

# Kiểm tra subscriber vẫn chạy
if kill -0 $SUBSCRIBER_PID 2>/dev/null; then
    echo "SUCCESS: Subscriber survived reconnection"
else
    echo "INFO: Subscriber may have restarted"
fi

# Cleanup
echo "Cleaning up..."
kill $PUBLISHER_PID 2>/dev/null
kill $SUBSCRIBER_PID 2>/dev/null

wait $PUBLISHER_PID 2>/dev/null
wait $SUBSCRIBER_PID 2>/dev/null

echo "Test completed"
