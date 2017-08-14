





typedef struct CHReadCtx{
    char* sql;
} CHReadCtx;

#ifdef INTERFACE_C_LINKAGE
extern "C" void ExecuteCHQuery(char *cstrQuery);

extern "C" void begin_ch_query(CHReadCtx *ctx);

extern "C" void end_ch_query(CHReadCtx *ctx);
#elif
extern void ExecuteCHQuery(char *cstrQuery);

extern void begin_ch_query(CHReadCtx *ctx);

extern void end_ch_query(CHReadCtx *ctx);

#endif
