


extern void TestConnection();


extern void ExecuteCHQuery(char *cstrQuery);

typedef struct CHReadCtx{
    char* sql;
} CHReadCtx;


extern "C" void begin_ch_query(CHReadCtx *ctx);

extern "C" void end_ch_query(CHReadCtx *ctx);
