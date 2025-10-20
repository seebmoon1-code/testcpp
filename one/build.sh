#!/bin/bash

echo "--- شروع کامپایل وب سرور C++ ---"

# دستور g++ اصلی
g++ -o server main.cpp -std=c++17 -Wall -Wextra -pedantic -pthread

# بررسی موفقیت کامپایل
if [ $? -eq 0 ]; then
    echo "✅ کامپایل موفقیت‌آمیز بود. فایل اجرایی: ./server"
else
    echo "❌ خطا در کامپایل."
fi
