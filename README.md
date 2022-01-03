Non-blocking Based Dropbox
---

- **目的**：使用select跟non-blocking實現single thread、single process的dropbox
- **功能**：
  - 一台server支援多台client連入，每個client有自己的使用者名稱（可重複）
  - 使用者名稱相同的client共享檔案空間，當一個終端上傳檔案時，所有同名client會自動下載該檔案，若有新版本檔案上傳，則會覆蓋原檔案，各client的檔案也會更新
  - 具體指令參照[non_blocking.pptx]（來自NYCU王協源教授網路程式設計概論課程）
- Makefile
  - `make`：編譯執行檔
  - `make clean`：可清除執行檔