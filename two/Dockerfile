
# ----------------------------------------------------------------------
# مرحله اول: مرحله ساخت (Build Stage)
FROM gcc:latest AS builder

# دایرکتوری کاری را در داخل کانتینر تنظیم کنید
WORKDIR /app

# فایل سورس کد را کپی کنید. (فایل باید در همان پوشه Dockerfile باشد)
COPY webserverbest.cpp . 

# کامپایل کد
# webserverbest: نام فایل اجرایی خروجی
RUN g++ webserverbest.cpp -o webserverbest -std=c++11 -pthread -lstdc++

# ----------------------------------------------------------------------

# مرحله دوم: مرحله اجرا (Runtime Stage)
FROM debian:stretch-slim
WORKDIR /app
# فایل اجرایی کامپایل شده را کپی کنید
COPY --from=builder /app/webserverbest /usr/local/bin/webserverbest
EXPOSE 8080
# دستوری که هنگام اجرای کانتینر اجرا می‌شود
CMD ["/usr/local/bin/webserverbest"]
