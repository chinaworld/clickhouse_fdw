





typedef struct CHReadCtx{
    char* sql;
    void* blocks;
    char** tupleValues;
    size_t natts;

    uint32_t currentBlock;
    uint32_t blockRows;
    uint32_t currentRow;
} CHReadCtx;

#ifdef INTERFACE_C_LINKAGE
extern "C" void ExecuteCHQuery(char *cstrQuery);

extern "C" void begin_ch_query(CHReadCtx *ctx);

extern "C" void end_ch_query(CHReadCtx *ctx);

extern "C" int read_ch_query(CHReadCtx *ctx);
#else
extern void ExecuteCHQuery(char *cstrQuery);

extern void begin_ch_query(CHReadCtx *ctx);

extern void end_ch_query(CHReadCtx *ctx);

extern int read_ch_query(CHReadCtx *ctx);
#endif
