# 古墓丽影：暗影 SOTTR — 0.4.7beta 无效

- exe=`SOTTR.exe` · GPU=**GTX 750 Ti**（Maxwell）· driver **59621** · build=**0.4.7beta**
- display 2560x1440 · storageFormat=28
- requested.mode=2 · nrSuccess=0
- bypass=**module-observer-uncertain**（start/end）· moduleObserverFault=**unknown-evaluate**
- 日志要点：RR ownership tracking became uncertain during direct late-core Feature Evaluate；non-null handle created before late `_nvngx` lifecycle observer；All future Feature 1 processing fails closed
- 过渡：waiting-feature1 → module-observer-uncertain ↔ master-disabled
- **硬件注意**：750 Ti 无 DLSS/Feature18 官方支持，可能属硬件门控或观察器误报；开单时标清，勿当 40/50 系同级兼容 bug
