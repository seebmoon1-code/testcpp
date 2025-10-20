# تنظیمات کامپایلر
CXX = g++

# پرچم‌های بهینه‌سازی، هشدار و استاندارد
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -O2

# پرچم لینک‌دهی برای نخ‌ها (Threads)
LDFLAGS = -pthread

# هدف اصلی ساخت (Build Target)
TARGET = server

# قانون پیش‌فرض: ساخت هدف
all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

# قانون تمیزکاری: حذف فایل‌های تولید شده
clean:
	rm -f $(TARGET)
