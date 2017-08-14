


extern void TestConnection();


extern void ExecuteCHQuery(char *cstrQuery);

typedef struct CHReadCtx{
    char* sql;
} CHReadCtx;


extern void begin_ch_query(CHReadCtx *ctx);

extern void end_ch_query(CHReadCtx *ctx);
