U0 Main()
{
  // Bsf: सबसे नीचे का set bit ढूंढो
  Print("%d\n", Bsf(8));
  // Bsr: सबसे ऊपर का set bit ढूंढो
  Print("%d\n", Bsr(8));
  // BCnt: popcount — कितने bit set हैं
  Print("%d\n", BCnt(7));
  // Bt: bit test करो
  Print("%d\n", Bt(0b1010, 1));
  Print("%d\n", Bt(0b1010, 0));
  // BFieldExtU32 से bit field निकालो
  Print("%d\n", BFieldExtU32(0b11010, 1, 3));
}
