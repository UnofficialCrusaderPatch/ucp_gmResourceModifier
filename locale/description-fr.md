# Gm Resource Modifier

**Auteur**: [TheRedDaemon](https://github.com/TheRedDaemon/ucp_gmResourceModifier)

Ce module permet de modifier les ressources GM1 déjà chargées par le jeu. Crusader traite les fichiers GM différemment des TGX : les TGX sont lus sur le disque à la demande, tandis que les GM sont chargés au démarrage. Intercepter les accès et modifier les chemins ne suffit donc pas. Les structures de gestion des fichiers GM en mémoire ont été identifiées pour permettre leur remplacement à la demande.

On peut remplacer un fichier entier ou des images individuelles, à condition de conserver le même type : SHC utilise plusieurs formats dans les fichiers GM1. Le module peut aussi créer une ressource à image unique de type « interface » à partir d’une image. Le convertisseur appelle des fonctions Windows ; sa prise en charge dépend donc du système et de Wine.

Ce module ne propose pas directement de fonctionnalités de jeu et sert de base technique. Consultez le README du dépôt pour plus d’informations.
