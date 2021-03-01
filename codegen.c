#include "9cc.h"

static int depth;

static void push(void) {
    printf("  push %%rax\n");
    depth++;
}

static void pop(char *arg) {
    printf("  pop %s\n", arg);
    depth--;
}

// 与えられたノードの絶対アドレスを計算する
// もし与えられたノードがメモリ上に存在しなかったらエラーを出力する
static void gen_addr(Node *node) {
    if (node->kind == ND_VAR) {
        int offset = (node->name - 'a' + 1) * 8;
        printf("  lea %d(%%rbp), %%rax\n", -offset);
        return;
    }

    error("左辺値ではありません");
}

// 抽象構文木にしたがって再帰的にアセンブリを出力する
void gen_expr(Node *node) {
    switch(node->kind) {
    case ND_NUM:
        printf("  mov $%d, %%rax\n", node->val);
        return;
    case ND_NEG:
        gen_expr(node->lhs);
        printf("  neg %%rax\n");
        return;
    case ND_VAR:
        gen_addr(node);
        printf("  mov (%%rax), %%rax\n");
        return;
    case ND_ASSIGN:
        gen_addr(node->lhs);
        push();
        gen_expr(node->rhs);
        pop("%rdi");
        printf("  mov %%rax, (%%rdi)\n");
        return;
    }

    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("%rdi");

    switch (node->kind) {
        case ND_ADD:
            printf("  add %%rdi, %%rax\n");
            return;
        case ND_SUB:
            printf("  sub %%rdi, %%rax\n");
            return;
        case ND_MUL:
            printf("  imul %%rdi, %%rax\n");
            return;
        case ND_DIV:
            printf("  cqo\n");
            printf("  idiv %%rdi\n");
            return;
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
            printf("  cmp %%rdi, %%rax\n");

            if (node->kind == ND_EQ)
                printf("  sete %%al\n");
            else if (node->kind == ND_NE)
                printf("  setne %%al\n");
            else if (node->kind == ND_LT)
                printf("  setl %%al\n");
            else if (node->kind == ND_LE)
                printf("  setle %%al\n");

            printf("  movzb %%al, %%rax\n");
            return;
    }

    error("正しくない式です");
}

static void gen_stmt(Node *node) {
    if (node->kind == ND_EXPR_STMT) {
        // expr以下の抽象構文木を下りながらコード生成
        gen_expr(node->lhs);
        return;
    }

    error("正しくない文です");
}

void codegen(Node *node) {
    // アセンブリの前半部分を出力
    printf("  .globl main\n");
    printf("main:\n");

    // プロローグ（アルファベット26文字のローカル変数のメモリを予め確保）
    printf("  push %%rbp\n");
    printf("  mov %%rsp, %%rbp\n");
    printf("  sub $208, %%rsp\n");  // 26文字 * 8 bit 分空けておく

    // stmtノードを順番に辿ってコード生成
    for (Node *n = node; n; n = n->next) {
        gen_stmt(n);
        assert(depth == 0);
    }

    // エピローグ
    printf("  mov %%rbp, %%rsp\n");
    printf("  pop %%rbp\n");

    // RAX に式を計算した結果が残っているので、
    // それをそのまま返す
    printf("  ret\n");
}