# Gm Resource Modifier

**Szerző**: [TheRedDaemon](https://github.com/TheRedDaemon/ucp_gmResourceModifier)

Ez a modul a játék már betöltött GM1-erőforrásainak módosítását teszi lehetővé. A Crusader másként kezeli a GM- és TGX-fájlokat: a TGX-et szükség szerint olvassa a lemezről, a GM-et viszont induláskor tölti be. Ezért nem elég elfogni a fájlműveleteket és átirányítani az útvonalakat. A betöltött GM-fájlok kezelési struktúráit azonosították az igény szerinti cseréhez.

Teljes fájlok vagy egyes képek is cserélhetők, de a típusnak azonosnak kell maradnia, mivel az SHC különböző formátumokat tárol GM1-fájlokban. A modul egy képből egyképes „interface” erőforrást is létrehozhat. Az átalakító Windows-függvényeket hív, ezért támogatása az operációs rendszertől és a Wine képességeitől függ.

A modul önálló játékfunkciók helyett alacsony szintű alapot biztosít. Részletek a forráskódtár README fájljában.
