
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <cstdlib> // برای تابع exit()

namespace fs = std::filesystem; // نامگذاری کوتاه برای std::filesystem

// تابع اصلی پروژه شما
// baseName: مسیر مبدأ، count: پسوند، targetPath: مسیر مقصد
void moveFiles(const fs::path& sourceDir, const std::string& targetExtension, const fs::path& destinationDir) {
    
    std::cout << "--- شروع عملیات انتقال ---" << std::endl;
    std::cout << "مسیر جستجو: " << sourceDir << std::endl;
    std::cout << "پسوند مورد نظر: " << targetExtension << std::endl;
    std::cout << "مسیر مقصد: " << destinationDir << std::endl;
    std::cout << "--------------------------" << std::endl;
    
    int filesFound = 0;
    
    // 1. بررسی و ایجاد مسیر مقصد
    // اگر مسیر مقصد وجود نداشت، آن را ایجاد می‌کنیم.
    try {
        if (!fs::exists(destinationDir)) {
            fs::create_directories(destinationDir);
            std::cout << "توجه: مسیر مقصد وجود نداشت و ایجاد شد." << std::endl;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "خطا: امکان ایجاد یا دسترسی به مسیر مقصد وجود ندارد." << std::endl;
        std::cerr << "جزئیات خطا: " << e.what() << std::endl;
        return;
    }

    // 2. پیمایش بازگشتی (جستجو در زیرپوشه‌ها)
    // recursive_directory_iterator تضمین می‌کند که تمام زیرپوشه‌ها هم جستجو شوند.
    for (const auto& entry : fs::recursive_directory_iterator(sourceDir)) {
        
        // فقط با فایل‌ها کار می‌کنیم، نه با پوشه‌ها
        if (entry.is_regular_file()) {
            
            // 3. بررسی پسوند فایل
            // از extension() برای گرفتن پسوند (مانند ".pdf") استفاده می‌کنیم
            if (entry.path().extension() == targetExtension) {
                
                filesFound++;
                fs::path sourcePath = entry.path();
                
                // ساخت مسیر نهایی برای فایل مقصد
                fs::path destinationPath = destinationDir / sourcePath.filename();

                // 4. عملیات انتقال فایل
                try {
                    // از rename استفاده می‌کنیم که به معنای انتقال (Move) در فایل سیستم است
                    fs::rename(sourcePath, destinationPath);
                    std::cout << "انتقال موفق: " << sourcePath.filename().string() 
                              << " به " << destinationDir.string() << std::endl;
                } catch (const fs::filesystem_error& e) {
                    std::cerr << "خطا در انتقال فایل " << sourcePath.filename().string() 
                              << ": " << e.what() << std::endl;
                }
            }
        }
    }
    
    std::cout << "--- عملیات به پایان رسید. تعداد فایل‌های منتقل شده: " 
              << filesFound << " ---" << std::endl;
}


// تابع اصلی که ورودی‌های ترمینال را دریافت می‌کند
int main(int argc, char* argv[]) {
    
    // آرگومان‌های خط فرمان باید 4 تا باشند (نام برنامه + 3 ورودی دیگر)
    if (argc != 4) {
        std::cerr << "!!! خطا: تعداد آرگومان ها اشتباه است !!!" << std::endl;
        std::cerr << "نحوه استفاده صحیح:" << std::endl;
        std::cerr << argv[0] << " <مسیر_مبدأ> <پسوند_فایل> <مسیر_مقصد>" << std::endl;
        std::cerr << "مثال: " << argv[0] << " /home/user/my_docs .pdf /home/user/Documents/PDFs" << std::endl;
        return 1; 
    }

    // استخراج آرگومان‌ها
    fs::path sourceDir(argv[1]);
    std::string targetExtension = argv[2]; // پسوند باید شامل نقطه باشد (مثل ".pdf")
    fs::path destinationDir(argv[3]);

    // فراخوانی تابع اصلی پروژه
    moveFiles(sourceDir, targetExtension, destinationDir);

    return 0; 
}
