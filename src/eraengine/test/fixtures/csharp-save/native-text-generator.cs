using MinorShift.Emuera.Sub;
class Program {
 static readonly string[] BaseNames="DAY MONEY ITEM FLAG TFLAG UP PALAMLV EXPLV EJAC DOWN RESULT COUNT TARGET ASSI MASTER NOITEM LOSEBASE SELECTCOM ASSIPLAY PREVCOM NOTUSE_14 NOTUSE_15 TIME ITEMSALES PLAYER NEXTCOM PBAND BOUGHT NOTUSE_1C NOTUSE_1D A B C D E F G H I J K L M N O P Q R S T U V W X Y Z NOTUSE_38 NOTUSE_39 NOTUSE_3A NOTUSE_3B".Split(' ');
 static void Main(string[] args) {
  if(args[0]=="generate") {
   Directory.CreateDirectory(args[1]);
   using(var w=new EraDataWriter(File.Create(args[1]+"/normal-text.sav"))) {
    w.Write(42L);w.Write(1808L);w.Write("C# native界😀");w.Write(0L);
    foreach(var n in BaseNames)w.Write(n=="DAY"?new long[]{7}:Array.Empty<long>());
    w.Write(new string[]{"native界😀"});w.EmuStart();
    for(int i=0;i<14;i++){if(i==5)w.WriteExtended("DA",new long[,]{{1,2,3},{4,5,6}});if(i==11)w.WriteExtended("TEXT_MATRIX",new long[,]{{11,12,13},{21,22,23}});if(i==13)w.WriteExtended("TEXT_CUBE",new long[,,]{{{1,2},{3,4}},{{5,6},{7,8}}});w.EmuSeparete();}
   }
   using(var w=new EraDataWriter(File.Create(args[1]+"/global-text.sav"))) {
    w.Write(42L);w.Write(1808L);w.Write(new long[]{999});w.Write(new string[]{"global界😀"});w.EmuStart();
    for(int i=0;i<6;i++){if(i==3)w.WriteExtended("USER_GLOBAL",new long[,]{{0,0,0},{0,0,77}});w.EmuSeparete();}
   }
  } else {
   using var r=new EraDataReader(File.OpenRead(args[1]));
   if(r.ReadInt64()!=42 || r.ReadInt64()!=1808)throw new Exception("metadata");
   bool global=args[2]=="global";
   if(global){long[] a=new long[1000];r.ReadInt64Array(a);string[] s=new string[1000];r.ReadStringArray(s);if(a[0]!=999)throw new Exception("global");}
   else {if(r.ReadString()!="text" || r.ReadInt64()!=0)throw new Exception("description/count");foreach(var n in BaseNames){long[] a=new long[10000];r.ReadInt64Array(a);if(n=="DAY" && a[0]!=7)throw new Exception("DAY");}string[] s=new string[1000];r.ReadStringArray(s);if(s[0]!="text界😀")throw new Exception("string");}
   if(!r.SeekEmuStart())throw new Exception("start");
   if(!global){r.ReadStringExtended();r.ReadInt64Extended();r.ReadStringArrayExtended();r.ReadInt64ArrayExtended();r.ReadStringArray2DExtended();r.ReadInt64Array2DExtended();r.ReadStringArray3DExtended();r.ReadInt64Array3DExtended();}
   r.ReadStringArrayExtended();r.ReadInt64ArrayExtended();r.ReadStringArray2DExtended();var matrices=r.ReadInt64Array2DExtended();r.ReadStringArray3DExtended();r.ReadInt64Array3DExtended();
   if(!matrices.ContainsKey(global?"USER_GLOBAL":"SAVED"))throw new Exception("user matrix");
   Console.WriteLine("Original C# EraDataReader verified "+args[1]);
  }
 }
}