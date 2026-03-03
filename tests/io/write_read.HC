U0 Main()
{
  U8* path = "/tmp/hc_test_wr.txt";
  // file में write करो
  I64 fd = FileOpen(path, 2);
  U8* msg = "hello\n";
  FileWrite(fd, msg, 6);
  FileClose(fd);
  // वापस read करो
  fd = FileOpen(path, 1);
  U8* buf = MAlloc(32);
  MemSet(buf, 0, 32);
  I64 n = FileRead(fd, buf, 31);
  FileClose(fd);
  Print("%s", buf);
  Free(buf);
  FileDel(path);
}
