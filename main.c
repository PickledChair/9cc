#include "chibicc.h"

static char *opt_o;

static char *input_path;

static void usage(int status) {
    fprintf(stderr, "chibicc [ -o <path> ] <file>\n");
    exit(status);
}

static void parse_args(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--help"))
            usage(0);

        if (!strcmp(argv[i], "-o")) {
            if (!argv[++i])
                usage(1);
            opt_o = argv[i];
            continue;
        }

        if (!strncmp(argv[i], "-o", 2)) {
            opt_o = argv[i] + 2;
            continue;
        }

        if (argv[i][0] == '-' && argv[i][1] != '\0')
            error("不正な引数です: %s", argv[i]);

        input_path = argv[i];
    }

    if (!input_path)
        error("入力元ファイルがありません");
}

static FILE *open_file(char *path) {
    if (!path || strcmp(path, "-") == 0)
        return stdout;

    FILE *out = fopen(path, "w");
    if (!out)
        error("出力先ファイルを開けませんでした: %s: %s", path, strerror(errno));
    return out;
}

int main(int argc, char **argv) {
    parse_args(argc, argv);

    // トークナイズする
    Token *tok = tokenize_file(input_path);

    // パースする
    Obj *prog = parse(tok);

    // ASTを走査してアセンブリを出力する
    FILE *out = open_file(opt_o);
    fprintf(out, ".file 1 \"%s\"\n", input_path);
    codegen(prog, out);
    return 0;
}
