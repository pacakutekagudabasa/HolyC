U0 Main()
{
  U8* path = "/tmp/hc_test_ex.txt";
  // अभी exist नहीं होना चाहिए
  Print("%d\n", FileExists(path));
  // file create करो
  I64 fd = FileOpen(path, 2);
  FileWrite(fd, "x", 1);
  FileClose(fd);
  // अब exist होना चाहिए
  Print("%d\n", FileExists(path));
  FileDel(path);
  // delete के बाद exist नहीं होना चाहिए
  Print("%d\n", FileExists(path));
}
