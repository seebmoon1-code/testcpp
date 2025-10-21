
#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib> // برای تابع exit()

// تعریف تابع اصلی پروژه شما
// این تابع منطق اصلی را انجام می دهد اما ورودی های واقعی از main می آیند
void start(const std::string& baseName, int count, const std::string& targetPath) {
    // تبدیل مسیر به یک شیء path برای کار با filesystem
    std::filesystem::path basePath(targetPath);

    std::cout << "--- شروع ساخت پوشه ها ---" << std::endl;
    std::cout << "نام پایه: " << baseName << std::endl;
    std::cout << "تعداد: " << count << std::endl;
    std::cout << "مسیر مقصد: " << targetPath << std::endl;
    std::cout << "-------------------------" << std::endl;

    if (!std::filesystem::exists(basePath)) {
        std::cerr << "خطا: مسیر مقصد وجود ندارد یا نامعتبر است: " << targetPath << std::endl;
        // تلاش برای ساخت مسیر مقصد در صورت لزوم (اختیاری)
        // std::filesystem::create_directories(basePath);
        return; 
    }

    for (int i = 1; i <= count; ++i) {
        // ساخت نام کامل پوشه (مثلاً 'ماه_01', 'ماه_02' و...)
        std::string folderName = baseName + "_" + (i < 10 ? "0" : "") + std::to_string(i);
        
        // ترکیب مسیر مقصد و نام پوشه جدید
        std::filesystem::path newFolderPath = basePath / folderName;

        try {
            // ایجاد دایرکتوری
            if (std::filesystem::create_directory(newFolderPath)) {
                std::cout << "موفقیت: پوشه ساخته شد: " << newFolderPath << std::endl;
            } else {
                // اگر دایرکتوری از قبل وجود داشته باشد
                std::cout << "توجه: پوشه از قبل وجود داشت: " << newFolderPath << std::endl;
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "خطا در ساخت پوشه " << folderName << ": " << e.what() << std::endl;
        }
    }
    std::cout << "--- عملیات ساخت پوشه ها با موفقیت به پایان رسید ---" << std::endl;
}

// تابع اصلی که ورودی های خط فرمان را دریافت می کند
int main(int argc, char* argv[]) {
    // 1. بررسی تعداد آرگومان ها
    // argc باید 4 باشد:
    // [0] نام برنامه (./start_app)
    // [1] نام پوشه (ماه)
    // [2] تعداد (10)
    // [3] مسیر (آدرس)

    if (argc != 4) {
        std::cerr << "!!! خطا: تعداد آرگومان ها اشتباه است !!!" << std::endl;
        std::cerr << "نحوه استفاده صحیح:" << std::endl;
        std::cerr << argv[0] << " <نام_پوشه> <تعداد> <مسیر_مقصد_کامل>" << std::endl;
        std::cerr << "مثال: " << argv[0] << " ماه 10 /home/user/Desktop/Test" << std::endl;
        return 1; // خروج با کد خطا
    }

    // 2. استخراج آرگومان ها
    std::string folderName = argv[1]; 
    int count;
    std::string targetPath = argv[3];

    // 3. تبدیل آرگومان دوم (تعداد) از رشته به عدد صحیح
    try {
        count = std::stoi(argv[2]);
        if (count <= 0) {
             std::cerr << "!!! خطا: تعداد باید یک عدد مثبت باشد !!!" << std::endl;
             return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "!!! خطا: آرگومان دوم (تعداد) باید یک عدد صحیح معتبر باشد !!!" << std::endl;
        return 1; // خروج با کد خطا
    }

    // 4. فراخوانی تابع اصلی پروژه شما
    start(folderName, count, targetPath);

    return 0; // خروج موفق
}
