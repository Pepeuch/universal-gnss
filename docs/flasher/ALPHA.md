# ESP32 ALPHA : publication et premiers essais

La cible du Web Flasher est
[pepeuch.github.io/universal-gnss/flasher.html](https://pepeuch.github.io/universal-gnss/flasher.html).
Le succès du déploiement et de sa vérification HTTP est la preuve de disponibilité,
pas la présence de ce lien. Au démarrage de cette préparation, cette URL retournait
HTTP 404 ; aucun déploiement public n’est effectué depuis la branche de préparation.

## Un déclenchement manuel, deux vrais firmwares

Après revue de la PR vers `dev`, intégration puis promotion explicitement décidée
vers `main`, le mainteneur lance **Actions → ESP32 alpha publication → Run workflow**
sur **main**. Il indique le SHA Git complet à construire, contenu dans `main`.
Ce déclenchement autorise à la fois la compilation et la publication de cet alpha ;
il ne faut pas éditer ensuite un registre d’approbation pour la même opération.

Le workflow :

1. Fige son contrôleur au commit du workflow et vérifie le SHA source sélectionné.
2. Construit réellement `s3-gateway` (W5500/GNSS/NTRIP/ESP-NOW) et
   `c3-rover-usb` (ESP-NOW, USB/UART runtime) avec le SDK 6.1 et les dépendances
   épinglés. Il utilise le runner natif existant : utilisateur non-root, sources
   archivées, aucun montage hôte/périphérique et réseau déconnecté après
   provisionnement, tests et deux reconstructions des quatre segments IDF.
3. Accepte uniquement les deux bundles complets et réussis pour ce même SHA.
   Il génère le registre depuis ce choix du mainteneur et un catalogue non vide.
4. Conserve durablement les ZIP S3/C3, le site complet, le catalogue, le registre,
   les sommes SHA256 et une archive exacte du code source correspondant dans une
   **GitHub Release préliminaire**. Les artefacts Actions intermédiaires expirent
   après sept jours ; ils ne sont pas l’archive permanente.
5. Retélécharge et vérifie ces assets avant de déployer Pages, puis contrôle les
   fichiers publics et les octets de chaque segment par HTTP.

Une compilation échouée, annulée ou ignorée empêche le job de publication.
Les bundles S3 doivent aussi attester les corrections de pile de démarrage et
d’allocation TLS : une ancienne image antérieure à ces corrections est refusée.
La qualification physique manquante n’empêche pas l’installation : l’interface
annonce **ALPHA — qualification matérielle en cours**, garde le consentement,
les contrôles de puce, les offsets/partitions et les empreintes. Aucun contenu
d’issue publique n’est exécuté ; aucun PAT n’est embarqué dans le navigateur.

Le jeton en écriture existe uniquement dans le job de publication. Les jobs de
build restent `contents: read`. Les permissions Pages/OIDC sont limitées au job
qui archive et déploie. Aucun workflow Docker Release n’est modifié.

## Tags et immutabilité

Le tag existant `0.7.5-a01` pointe vers
`758273b28869bf7aaaf1246d1d8809fe582e3909`, qui précède les applications natives
ESP32. Il ne convient pas au firmware actuel et reste intact.

Le champ optionnel du workflow accepte ce tag seulement s’il correspond
exactement au SHA choisi. Sinon le workflow emploie le tag dédié
`esp32-alpha-<12 premiers caractères du SHA>`. Il le crée au SHA choisi ou
vérifie un tag existant identique. Il ne déplace jamais un tag et n’écrase aucun
asset. Un conflit de contenu arrête la publication. Ce nom de publication
expérimentale ne modifie pas la version fonctionnelle globale UG et ne correspond
pas au motif `v*` du workflow Docker Release.

Le workflow peut être relancé pour une panne de déploiement ; il vérifie les
assets déjà présents avant de les réutiliser. Si un nouveau contrôleur ou des
métadonnées reconstruits divergent de l’archive immuable, sélectionner un nouveau
commit source pour une nouvelle publication plutôt que remplacer les anciens
assets. Le site de cette première chaîne expose les deux images du SHA publié ;
les anciennes publications restent téléchargeables dans les Releases.

## GitHub Pages

Si Pages n’est pas déjà activé :
**Repository → Settings → Pages → Build and deployment → Source → GitHub Actions**.
L’environnement `github-pages` doit autoriser le déploiement depuis `main`.
Aucune règle de protection ni approbation d’environnement supplémentaire n’est
ajoutée par ce lot. Une règle GitHub déjà configurée reste sous contrôle du
mainteneur ; les journaux du workflow indiquent si elle bloque le déploiement.

Le workflow utilise `configure-pages@v5`, `upload-pages-artifact@v3` et
`deploy-pages@v4`. Le site contient seulement les ressources publiques prévues et
les bundles vérifiés ; aucune configuration locale, sauvegarde Flash ou preuve
privée ne doit être placée dans ce répertoire.

Une préparation locale avec de vrais bundles déjà qualifiés est possible depuis
la racine, sans publier :

```bash
python3 tools/branches/esp32_alpha.py stage --source-sha COMMIT_MAIN_COMPLET \
  --bundle /tmp/bundle-s3 --bundle /tmp/bundle-c3 --output /tmp/alpha-site-neuf
```

Cette commande ne compile ni ne flashe ; les bundles doivent provenir des
véritables builds `native/ci.py --bundle-output`. Un catalogue incomplet est refusé.
Les commandes de publication restent réservées au workflow manuel depuis main.

## Installation et configuration locale

Choisir la carte exacte, attendre la vérification de tous les segments, puis
confirmer l’installation complète. Les images expérimentales ne sont pas des
preuves de RTK FIX, de portée radio ou de réception GNSS. Sauvegarder la Flash/NVS
avant tout remplacement d’un firmware existant ; la conservation d’une ancienne
configuration n’est pas annoncée comme une mise à jour qualifiée.

Le C3 générique choisit USB/UART et GPIO/débit depuis NVS. Aucun pair, clé, canal
ou fichier utilisateur n’entre dans la ROM publique. Le S3 garde le câblage W5500
soudé et exige une configuration GNSS réelle avant activation.

Après installation, la console USB du S3 permet le provisionnement local de
l’identité d’administration et TLS ; voir la
[procédure native](../../embedded/esp32/migration/README.md) et le
[contrat portable](../../embedded/esp32/PORTABLE_CONFIG.md).
Le bouton de restauration locale utilise ce protocole ou la maintenance C3,
avec aperçu/confirmation, sans reset ni RUN automatique et sans envoi du fichier
vers GitHub. Les données de contrôle ne partagent pas un flux RTCM actif.

## Activations dans la WebUI du S3

Les formulaires GNSS/UART et NTRIP restent configurables sans récepteur ou
serveur raccordé. La section « Activer / désactiver les fonctions » expose aussi
ESP-NOW, le caster local et les modes Wi-Fi. Un secret laissé vide est conservé ;
désactiver ESP-NOW conserve l’association. Les contraintes de GPIO, de clé/pair
et d’incompatibilité STA/ESP-NOW restent vérifiées avant sauvegarde.

L’activation demandée et l’état observé sont distincts : un périphérique absent
peut laisser un service en attente ou en erreur. Aucun paquet reçu ni état RTK
n’est déduit d’un interrupteur. MQTT n’a pas encore de service natif et LoRa
reste différé ; l’interface les signale indisponibles. La sortie RTCM UART
autonome appartient actuellement au profil Rover ; l’UART S3 est le transport
GNSS configurable. Les changements Wi-Fi et la réactivation radio demandent un
redémarrage explicite.

Les observations et limites du premier essai physique sont consignées dans le
[bilan S3](../../embedded/esp32/HARDWARE_ALPHA.md). Elles ne qualifient pas les
cartes C3/C6 ni un déploiement public du portail.
