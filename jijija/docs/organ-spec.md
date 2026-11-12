# مشخصات شناسنامه اندام (Organ Spec v1)

> **یادداشت بازسازی:** نسخه قبلی این سند بدون خوانده‌شدن بازنویسی شد و از بین رفت. این نسخه از روی پارسر واقعی (`firmware/creature/src/organ.c`، `organ.h`) و تست‌هایش (`test/test_organ.c`) نوشته شده است و همان چیزی را توصیف می‌کند که پارسر می‌پذیرد.

هر اندام یک فایل TOML در `genome/organs/` دارد. زیرمجموعه ساده‌ای از TOML پشتیبانی می‌شود: بخش‌ها، رشته، عدد صحیح (بدون اعشار)، `true/false`، آرایه، توضیح با `#`، CRLF.

## بخش‌ها و کلیدها

```toml
[organ]               # الزامی
id       = "light"    # الزامی؛ حروف کوچک/رقم/_ ، حداکثر ۱۵ نویسه
kind     = "sensor"   # sensor | actuator | internal
unit     = "adc_count"   # ≤ ۳۱ بایت
range    = [0, 4095]  # range[0] <= range[1]
rate_hz  = 100        # ≥ 0
channels = 1          # ≥ 1، پیش‌فرض 1
version  = 1          # پیش‌فرض 1

[hardware]            # اختیاری؛ کلیدهای ناشناخته نادیده گرفته می‌شوند
part = "..."          # به ۶۳ بایت بریده می‌شود (روی مرز UTF-8)
bus  = "adc"
note = "..."

[reflex]              # الزامی
none        = true    # سنسور/داخلی: true. ابزار: false همراه حداقل یک سقف
max_rate_hz = ...     # سقف‌های رفلکس (فقط وقتی none=false)
max_duty    = ...
min_off_ms  = ...

[vocabulary]          # الزامی
words = ["read-light"]   # حداکثر ۸ کلمه، هر کدام ≤ ۳۱ بایت
```

## قواعد اعتبارسنجی (از تست‌ها)

- ابزار (`actuator`) با `none = true` یا `none = false` بدون هیچ سقفی رد می‌شود (`ORGAN_ERR_REFLEX`)؛ `none = true` همراه سقف هم رد می‌شود. **رفلکس سمت بدن است و موجود نمی‌تواند آن را بالاتر ببرد.**
- کلید ناشناخته در `[organ]`، `[reflex]`، `[vocabulary]` خطاست (با شماره خط)؛ بخش ناشناخته هم.
- رجیستری حداکثر ۸ اندام و id تکراری ندارد.
- پارسر بدون `malloc` و بدون وابستگی به Zephyr است؛ روی PC و MCU یک کد است.

## ابزار و تست

```
cd firmware/creature && make test   # ۸۶ تست پارسر + اعتبار فایل‌های واقعی genome/organs
cd firmware/creature && make host   # «organ list» روی PC
```

سنسورهای مرجع لاگر (BH1750، DS18B20) جزو بدن موجود نیستند و در `genome/organs/` نمی‌آیند؛ اگر کاربر آن‌ها را بگذارد فقط در لاگ (organ 8 و 9) ثبت می‌شوند.

## وضعیت فایل‌های `genome/organs/*.toml`

این شش فایل (light, thermo, touch, hunger, led, buzzer) **بازسازی شده‌اند** (نسخه قبلی بازنویسی شد). مقادیر مهم‌ترین‌ها (مثل `channels = 6` برای touch و `min_off_ms = 20` برای buzzer) از تست‌ها قطعی‌اند؛ سقف `max_duty = 128` برای led/buzzer و بقیه مقادیر انتخاب جدید است و باید تأیید شود.
