#include "9cc.h"

static int depth;

static int count(void) {
    static int i = 1;
    return i++;
}

static void push(void) {
    printf("  push %%rax\n");
    depth++;
}

static void pop(char *arg) {
    printf("  pop %s\n", arg);
    depth--;
}

// nを最も近いalignの倍数に切り上げる。例えば、
// align_to(5, 8)は8を返し、align_to(11, 8)は16を返す
static int align_to(int n, int align) {
    return (n + align - 1) / align * align;
}

// 与えられたノードの絶対アドレスを計算する
// もし与えられたノードがメモリ上に存在しなかったらエラーを出力する
static void gen_addr(Node *node) {
    if (node->kind == ND_VAR) {
        printf("  lea %d(%%rbp), %%rax\n", node->var->offset);
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
    switch (node->kind) {
    case ND_IF: {
        int c = count();
        gen_expr(node->cond);
        printf("  cmp $0, %%rax\n");
        printf("  je .L.else.%d\n", c);
        gen_stmt(node->then);
        printf("  jmp .L.end.%d\n", c);
        printf(".L.else.%d:\n", c);
        if (node->els)
            gen_stmt(node->els);
        printf(".L.end.%d:\n", c);
        return;
    }
    case ND_FOR: {
        int c = count();
        gen_stmt(node->init);
        printf(".L.begin.%d:\n", c);
        if (node->cond) {
            gen_expr(node->cond);
            printf("  cmp $0, %%rax\n");
            printf("  je .L.end.%d\n", c);
        }
        gen_stmt(node->then);
        if (node->inc)
            gen_expr(node->inc);
        printf("  jmp .L.begin.%d\n", c);
        printf(".L.end.%d:\n", c);
        return;
    }
    case ND_BLOCK:
        // stmtノードを順番に辿ってコード生成
        for (Node *n = node->body; n; n = n->next)
            gen_stmt(n);
        return;
    case ND_RETURN:
        gen_expr(node->lhs);
        // .L.return ラベルにジャンプする
        printf("  jmp .L.return\n");
        return;
    case ND_EXPR_STMT:
        // expr以下の抽象構文木を下りながらコード生成
        gen_expr(node->lhs);
        return;
    }

    error("正しくない文です");
}

// 各ローカル変数のoffsetにオフセットを代入する
static void assign_lvar_offsets(Function *prog) {
    int offset = 0;
    for (Obj *var = prog->locals; var; var = var->next) {
        offset += 8;
        var->offset = -offset;
    }
    prog->stack_size = align_to(offset, 16);
}

void codegen(Function *prog) {
    assign_lvar_offsets(prog);

    // main関数
    printf("  .globl main\n");
    printf("main:\n");

    // プロローグ
    printf("  push %%rbp\n");
    printf("  mov %%rsp, %%rbp\n");
    printf("  sub $%d, %%rsp\n", prog->stack_size);  // 関数フレームの確保

    // コード生成
    gen_stmt(prog->body);
    assert(depth == 0);

    // エピローグ
    printf(".L.return:\n");  // return文からの飛び先がここ
    printf("  mov %%rbp, %%rsp\n");
    printf("  pop %%rbp\n");

    // RAX に式を計算した結果が残っているので、
    // それをそのまま返す
    printf("  ret\n");
}
