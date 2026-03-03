// skip-interpret — interpreter mode skip करो
// Test करो कि postfix cast user-defined class names को type tokens मानता है
class Node { I64 val; };
U0 Main() {
    Node n;
    n.val = 77;
    Node* p = &n;
    // pointer को same class type में cast करो (no-op cast, parser fix test करो)
    Node* q = p(Node*);
    "%d\n", q->val;
}
