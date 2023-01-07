#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
//	TK_RESERVED, //記号
//	TK_NUM,	     //整数トークン
//	TK_EOF,	     //入力の終わりを示すトークン
	ND_ADD, // +
	ND_SUB,	// -
	ND_MUL,	// *
	ND_DIV,	// /
	ND_NUM,	// 整数
} NodeKind;

typedef struct Node Node;


struct Token {
	TokenKind kind; //トークンの型
	Token *next;	//次の入力トークン
	int val;	//kindがTK_NUMの場合、その数値
	char *str;	//トークン文字列
	int len;	//トークンの長さ
};


struct Node {
	NodeKind kind; // ノードの型
	Node *lhs;     // 左辺	
	Node *rhs;     // 右辺
	int val;       // kindがND_NUMの場合のみ使う
};

Node *new_node(NodeKind kind, Node *lhs, Node *rhs) {
	Node *node = calloc(1, sizeof(Node));
	node->kind = kind;
	node->lhs = lhs;
	node->rhs = rhs;
	return node;
}

Node *new_node_num(int val) {
	Node *node = calloc(1, sizeof(Node));
	node->kind = ND_NUM;
	node->val = val;
	return node;
}

Node *expr() {
	Node *node = mul();

	for(;;) {
		if (consume('+'))
			node = new_node(ND_ADD, node, mul());
		else if (consume('-'))
			node = new_node(ND_SUB, node, mul());
		else
			return node;
	}
}

Node *mul() {
	Node *node = primary();

	for (;;) {
		if (consume('*'))
			node = new_node(ND_MUL, node, primary());
		else if (consume('/'))
			node = new_node(ND_DIV, node, primary());
		else
			return node;
	}
}

Node *primary() {
	// 次のトークンが "(" なら、"(" expr ")"のはず
	if (consume('(')) {
		Node *node = expr();
		expect(')');
		return node;
	}

	//違うなら数値のはず
	return new_node_num(expect_number());
}

void gen(Node *node) {
	if (node->kind == ND_NUM) {
		printf("	push %d\n", node->val);
		return;
	}

	gen(node->lhs);
	gen(node->rhs);

	printf("	pop rdi\n");
	printf("	pop rax\n");

	switch (node->kind) {
		case ND_ADD:
			printf("	add rax, rdi\n");
			break;
		case ND_SUB:
			printf("	sub rax, rdi\n");
			break;
		case ND_MUL:
			printf("	imul rax, rdi\n");
			break;
		case ND_DIV:
			printf("	cqo\n");
			printf("	idiv rdi\n");
			break;
	}

	printf("	push rax\n");
}

//現在着目しているトークン
Token *token;

//エラー報告関数
//printfと同じ引数を取る
void error(char *fmt, ...){
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	exit(1);
}

//次のトークンが期待している記号なら、トークンを1つ読み進めて真を返す
//それ以外なら偽を返す
bool consume(char *op){
	if (token->kind != TK_RESERVED || 
	strlen(op) != token->len ||
	memcmp(token->str, op, token->len))
		return false;
	token = token->next;
	return true;
}

//次のトークンが期待している記号の場合、トークンを1つ読み進める
//それ以外の場合エラーを報告する
void expect(char op){
	if(token->kind != TK_RESERVED || token->str[0] != op)
		error("'%c'ではありません", op);
	token = token->next;
}

//次のトークンが数値の場合、トークンを1つ読み進めてその数値を返す
//それ以外ならエラーを報告する
int expect_number(){
	if (token->kind != TK_NUM)
		error("数ではありません");
	int val = token->val;
	token = token->next;
	return val;
}

bool at_eof(){
	return token->kind == TK_EOF;
}

//新しいトークンを作成、curにつなげる
Token *new_token(TokenKind kind, Token *cur, char *str){
	Token *tok = calloc(1, sizeof(Token));
	tok->kind = kind;
	tok->str = str;
	cur->next = tok;
	return tok;
}

//入力文字列pをトークナイズしてそれを返す
Token *tokenize(char *p){
	Token head;
	head.next = NULL;
	Token *cur = &head;

	while(*p){
		//空白文字をスキップ
		if(isspace(*p)){
			p++;
			continue;
		}

		if(*p == '+' || *p == '-'){
			cur = new_token(TK_RESERVED, cur, p++);
			continue;
		}

		if(isdigit(*p)){
			cur = new_token(TK_NUM, cur, p);
			cur->val = strtol(p, &p, 10);
			continue;
		}

		error("トークナイズできません");
	}

	new_token(TK_EOF, cur, p);
	return head.next;
}

//入力プログラム
char *user_input;

//エラー箇所を報告する
void error_at(char *loc, char *fmt, ...){
	va_list ap;
	va_start(ap, fmt);

	int pos = loc - user_input;
	fprintf(stderr, "%s\n", user_input);
	fprintf(stderr, "%*s", pos, " "); //pos個の空白を出力
	fprintf(stderr, "^ ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	exit(1);
}

Node *unary() {
	if (consume('+'))
		return primary();
	if (consume('-'))
		return new_node(ND_SUB, new_node_num(0), primary());
	return primary();

int main(int argc, char **argv){
	if(argc != 2){
		error("引数の個数が正しくありません");
		return 1;
	}

	//トークナイズ&パースする
	user_input = argv[1];
	token = tokenize(user_input);
	Node *node = expr();

	//アセンブリの前半部分を出力
	printf(".intel_syntax noprefix\n");
	printf(".globl main\n");
	printf("main:\n");

	//抽象構文木を下りながらコード生成
	gen(node);

	//スタックトップに式全体の値が残っているはずなので
	//それをRAxにロードして関数からの返り値とする
	printf("	pop rax\n");
	printf("	ret\n");
	return 0;
}
