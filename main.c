#include "9cc.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "引数の個数が正しくありません\n");
        return 1;
    }

    // トークナイズする
    Token *tok = tokenize(argv[1]);

    // パースする
    Node *node = parse(tok);

    codegen(node);
    return 0;
}