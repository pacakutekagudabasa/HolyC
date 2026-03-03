// skip-interpret skip-jit skip-aot — formatter mode में test करो
U0 Foo() { "foo\n"; }


U0 Bar() { "bar\n"; }
U0 Main() { Foo(); Bar(); }
