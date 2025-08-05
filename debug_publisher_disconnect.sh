#!/bin/bash

# Script debug và monitor cho nginx-rtmp publisher disconnection issues
# Sử dụng: ./debug_publisher_disconnect.sh

LOG_FILE="/var/log/nginx/error.log"
RTMP_ACCESS_LOG="/var/log/nginx/rtmp_access.log"
OUTPUT_FILE="/tmp/publisher_debug_$(date +%Y%m%d_%H%M%S).log"

echo "=== NGINX-RTMP PUBLISHER DISCONNECT DEBUG SCRIPT ===" | tee $OUTPUT_FILE
echo "Started at: $(date)" | tee -a $OUTPUT_FILE
echo "Analyzing logs: $LOG_FILE" | tee -a $OUTPUT_FILE
echo "Output file: $OUTPUT_FILE" | tee -a $OUTPUT_FILE
echo "" | tee -a $OUTPUT_FILE

# Function to analyze publisher disconnections
analyze_publisher_disconnects() {
    echo "=== ANALYZING PUBLISHER DISCONNECTS ===" | tee -a $OUTPUT_FILE
    
    # Look for idle timer drops
    echo "--- Idle Timer Disconnects ---" | tee -a $OUTPUT_FILE
    grep -n "drop idle publisher\|dropping idle publisher" $LOG_FILE | tail -20 | tee -a $OUTPUT_FILE
    
    # Look for publisher status changes
    echo "" | tee -a $OUTPUT_FILE
    echo "--- Publisher Status Changes ---" | tee -a $OUTPUT_FILE  
    grep -n "processing publisher status change" $LOG_FILE | tail -20 | tee -a $OUTPUT_FILE
    
    # Look for stream active/inactive changes
    echo "" | tee -a $OUTPUT_FILE
    echo "--- Stream Active/Inactive Changes ---" | tee -a $OUTPUT_FILE
    grep -n "stream.*active set to" $LOG_FILE | tail -20 | tee -a $OUTPUT_FILE
    
    # Look for timer events
    echo "" | tee -a $OUTPUT_FILE
    echo "--- Timer Events ---" | tee -a $OUTPUT_FILE
    grep -n "idle timer\|setting idle timer\|deleting idle timer" $LOG_FILE | tail -20 | tee -a $OUTPUT_FILE
}

# Function to check current nginx processes and connections
check_nginx_status() {
    echo "=== NGINX STATUS ===" | tee -a $OUTPUT_FILE
    
    echo "--- Nginx Processes ---" | tee -a $OUTPUT_FILE
    ps aux | grep nginx | grep -v grep | tee -a $OUTPUT_FILE
    
    echo "" | tee -a $OUTPUT_FILE
    echo "--- Active RTMP Connections ---" | tee -a $OUTPUT_FILE
    netstat -an | grep :1935 | tee -a $OUTPUT_FILE
    
    echo "" | tee -a $OUTPUT_FILE
    echo "--- Recent RTMP Access Log ---" | tee -a $OUTPUT_FILE
    if [ -f "$RTMP_ACCESS_LOG" ]; then
        tail -10 $RTMP_ACCESS_LOG | tee -a $OUTPUT_FILE
    else
        echo "RTMP access log not found at $RTMP_ACCESS_LOG" | tee -a $OUTPUT_FILE
    fi
}

# Function to show recent error patterns
show_error_patterns() {
    echo "=== ERROR PATTERNS (Last 50 lines) ===" | tee -a $OUTPUT_FILE
    
    echo "--- Finalize Session Events ---" | tee -a $OUTPUT_FILE
    grep -n "finalize.*session\|finalizing session" $LOG_FILE | tail -10 | tee -a $OUTPUT_FILE
    
    echo "" | tee -a $OUTPUT_FILE
    echo "--- Connection Errors ---" | tee -a $OUTPUT_FILE
    grep -n "connection.*error\|no connection" $LOG_FILE | tail -10 | tee -a $OUTPUT_FILE
    
    echo "" | tee -a $OUTPUT_FILE
    echo "--- AV Handler Errors ---" | tee -a $OUTPUT_FILE
    grep -n "av handler.*error\|av send failed" $LOG_FILE | tail -10 | tee -a $OUTPUT_FILE
}

# Function to provide recommendations
provide_recommendations() {
    echo "=== RECOMMENDATIONS ===" | tee -a $OUTPUT_FILE
    
    # Count idle timer disconnects in last hour
    IDLE_DROPS=$(grep "$(date +%Y/%m/%d\ %H)" $LOG_FILE | grep -c "drop idle publisher\|dropping idle publisher")
    
    echo "Publisher disconnects in last hour: $IDLE_DROPS" | tee -a $OUTPUT_FILE
    
    if [ $IDLE_DROPS -gt 0 ]; then
        echo "" | tee -a $OUTPUT_FILE
        echo "*** PUBLISHER ĐƯỢC NGẮT DO IDLE TIMER ***" | tee -a $OUTPUT_FILE
        echo "Khuyến nghị:" | tee -a $OUTPUT_FILE
        echo "1. Tăng drop_idle_publisher timeout trong nginx.conf:" | tee -a $OUTPUT_FILE
        echo "   drop_idle_publisher 600s;  # 10 phút thay vì 300s" | tee -a $OUTPUT_FILE
        echo "2. Hoặc tắt hoàn toàn: drop_idle_publisher off;" | tee -a $OUTPUT_FILE
        echo "3. Kiểm tra publisher có đang gửi data liên tục không" | tee -a $OUTPUT_FILE
        echo "4. Xem log để đảm bảo av packets được xử lý đúng" | tee -a $OUTPUT_FILE
    fi
    
    # Check for connection issues
    CONN_ERRORS=$(grep "$(date +%Y/%m/%d\ %H)" $LOG_FILE | grep -c "no connection\|connection.*error")
    
    if [ $CONN_ERRORS -gt 0 ]; then
        echo "" | tee -a $OUTPUT_FILE
        echo "*** VẤN ĐỀ KẾT NỐI MẠNG ***" | tee -a $OUTPUT_FILE
        echo "Connection errors in last hour: $CONN_ERRORS" | tee -a $OUTPUT_FILE
        echo "Khuyến nghị:" | tee -a $OUTPUT_FILE
        echo "1. Kiểm tra network stability" | tee -a $OUTPUT_FILE
        echo "2. Tăng buffer size: buffer 10s;" | tee -a $OUTPUT_FILE
        echo "3. Enable keep_connections: keep_connections on;" | tee -a $OUTPUT_FILE
    fi
}

# Function to monitor in real-time
monitor_realtime() {
    echo "=== REAL-TIME MONITORING ===" | tee -a $OUTPUT_FILE
    echo "Monitoring for 30 seconds... (Ctrl+C to stop)" | tee -a $OUTPUT_FILE
    
    timeout 30 tail -f $LOG_FILE | grep --line-buffered -E "(idle timer|drop.*publisher|av handler|finalize.*session)" | while read line; do
        echo "$(date '+%H:%M:%S'): $line" | tee -a $OUTPUT_FILE
    done
}

# Main execution
echo "1. Analyzing publisher disconnects..."
analyze_publisher_disconnects

echo ""
echo "2. Checking nginx status..."
check_nginx_status

echo ""
echo "3. Showing error patterns..."
show_error_patterns

echo ""
echo "4. Providing recommendations..."
provide_recommendations

echo ""
echo "5. Starting real-time monitoring..."
monitor_realtime

echo "" | tee -a $OUTPUT_FILE
echo "=== DEBUG COMPLETED ===" | tee -a $OUTPUT_FILE
echo "Finished at: $(date)" | tee -a $OUTPUT_FILE
echo "Full report saved to: $OUTPUT_FILE" | tee -a $OUTPUT_FILE

# Show summary
echo ""
echo "=== SUMMARY ==="
echo "Check the generated report: $OUTPUT_FILE"
echo ""
echo "Quick fixes to try:"
echo "1. Edit nginx.conf:"
echo "   drop_idle_publisher 600s;  # Tăng timeout"
echo "   keep_connections on;       # Giữ kết nối"
echo ""
echo "2. Restart nginx: sudo systemctl restart nginx"
echo ""
echo "3. Monitor with: tail -f $LOG_FILE | grep 'idle timer'"
