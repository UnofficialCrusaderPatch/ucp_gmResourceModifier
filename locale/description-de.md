# Gm Resource Modifier

**Autor**: [TheRedDaemon](https://github.com/TheRedDaemon/ucp_gmResourceModifier)

Dieses Modul ermöglicht es, die bereits geladenen GM1-Ressourcen des Spiels zu ersetzen. Crusader behandelt GM-Dateien anders als TGX-Dateien: TGX wird bei Bedarf von der Festplatte geladen, GM dagegen bereits beim Start. Deshalb reicht es nicht, Dateizugriffe abzufangen und Pfade umzuleiten. Die Verwaltungsstrukturen der geladenen GM-Dateien wurden untersucht, um sie bei Bedarf ersetzen zu können.

Es lassen sich ganze Dateien oder einzelne Bilder austauschen. Der Typ muss übereinstimmen, da SHC unterschiedliche Formate in GM1-Dateien speichert. Zusätzlich kann das Modul aus einem Bild eine einzelne Bildressource des Typs „Interface“ erzeugen. Der Konverter verwendet Windows-Funktionen; die Unterstützung hängt daher vom Betriebssystem und von Wine ab.

Das Modul bietet selbst keine direkt nutzbaren Spielfunktionen und dient als technische Grundlage. Weitere Informationen stehen in der README des Repositorys.
