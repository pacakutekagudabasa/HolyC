// skip-jit — JIT mode skip करो
// skip-aot — AOT mode skip करो
U0 Main() {
  DirMk("/tmp/hcc_dir_test");
  "%d\n", DirExists("/tmp/hcc_dir_test");
  FileRename("/tmp/hcc_dir_test", "/tmp/hcc_dir_test2");
  "%d\n", DirExists("/tmp/hcc_dir_test2");
}
