# Universal GNSS ESP32 Web Flasher — integration and hosting

## À placer dans le dépôt UG

- `docs/flasher.html` : interface et intégration ESP Web Tools v10.4.0.
- `docs/flasher/catalog.json` : catalogue public initialement **vide**.
- Le badge du README pointe vers [le portail source](../flasher.html), sans présumer un déploiement Pages.
- Activer GitHub Pages sur `docs/` seulement lorsque le lot est autorisé à être publié.
- La nouvelle URL `https://pepeuch.github.io/universal-gnss/flasher.html` est un **emplacement envisagé**, pas une page actuellement publiée.

## Périmètre V1

- S3 Waveshare W5500: firmware Gateway avec GNSS local ou source NTRIP (mode à sélectionner à l'exécution).
- C3 SuperMini: rover USB ou UART1 TX explicite GPIO4/5/6/7/10 (115200–921600 baud).
- Canal ESP-NOW 1–11 et MAC unicast pair sont actuellement gravés au build native.
- Types GNSS u-blox/Unicore choisis dans l'interface, mais le firmware est multiconstructeur : la sélection relève de la configuration à l'exécution, pas d'un build de ROM distinct.
- Pas de C6, LoRa, Mammotion, autre carte ou firmware historique.

## Ce qui est réellement implémenté dans ce prototype

- Sélection des profils, contraintes d'options et URL d'issue préremplie pour demander le build manquant.
- Lecture d'un catalogue **statique** des images publiées; bouton Web Serial uniquement pour une entrée avec métadonnées minimales cohérentes.
- Export JSON de la configuration sans secrets.
- **Aucun firmware n'est fourni. Aucune compilation n'est déclenchée par ce prototype. Aucun flash ni deployment n'a été testé.**
- Les hash SHA-256 sont des métadonnées de publication, pas une preuve cryptographique côté navigateur; la CI doit les vérifier et les signer/attester avant la publication.

## CI à la demande : bloc à implémenter séparément

GitHub Pages est statique. Une page anonyme ne peut pas déclencher directement `workflow_dispatch` sans jeton GitHub `Actions: write`. Ne jamais embarquer un PAT dans le JavaScript.

Flux proposé:
1. Le navigateur normalise la sélection et crée un identifiant de build incluant **SHA source validé**, version IDF et options qui modifient les octets binaires (carte, UART, canal, MAC). La source ne peut pas être arbitraire/non revue.
2. La page consulte le catalogue. Si la ROM existe, afficher la version, les empreintes, la provenance et le bouton Flash.
3. Sinon, une issue GitHub authentifiée peut solliciter le build. Pour un lancement AUTOMATIQUE, ajouter un workflow de réception de demandes avec contrôles stricts (actor/quota, whitelist, rate-limit, aucune insertion libre dans shell, aucune exécution de code provenant de l'issue). Une validation mainteneur est l'option de départ la plus sûre.
4. Un workflow distinct utilise le build.py UG officiel dans le SDK6.1 épinglé; il valide la partition, le bin ELF, le rôle, les tests pertinents, la reproductibilité et produit les binaires nécessaires: bootloader, partition-table, otadata si requise, application. **Les offsets proviennent du build ESP-IDF/flash_args, jamais d'un ancien manifeste.**
5. Après les contrôles, publication des images et manifestes ESP Web Tools depuis un job de publication isolé, avec permissions limitées. Le manifeste doit résoudre depuis Pages et tous les fichiers doivent avoir les bons CORS si distribués depuis une autre origine.
6. Mise à jour atomique du catalogue. Les builds validés pourront être réutilisés si la clé est identique. Utiliser un stockage durable adapté (GitHub Releases ou stockage externe), pas les artifacts Action comme catalogue permanent : les artifacts publics sont limités à 90 jours.

**Attention :** les options 'MAC pair' génèrent des images par appareil et une MAC publique peut être sensible du point de vue de l'identification. À terme il serait beaucoup plus efficace de provisionner le MAC/canal dans les réglages locaux NVS par USB/Web après flash, et de garder une seule image générique par carte + rôle + version. Ce n'est pas encore le contrat du firmware natif, donc ne pas le supposer fonctionnel.

**Attention :** `workflow_dispatch` ne se déclenche que si le workflow est sur la **branche par défaut** (actuellement `main`) ; le frontend seul sur `feat/esp32` ne suffit pas. Déployer ce workflow est un changement distinct, à contrôler et approuver.

## Critères de sécurité avant premier flash réel

- Pinout Waveshare S3 W5500 confirmé, pas seulement `GPIO{11,12,13,14,10,9}` compilé ; broches RX/TX GNSS sans conflit octal PSRAM.
- Référence C3 et brochage UART/USB réels qualifiés.
- Puce cible, Flash 16MB S3 vs 4MB C3, offsets/partition et mode install vs upgrade explicités.
- Si NVS/identité existante doit être conservée, ne jamais appeler 'update sûr' un full reflash sans test de compatibilité et sauvegarde.
- Aucun code non confirmé ne doit être présenté comme 'validated hardware'.

## Outil d'empaquetage local (sans publication)

Le script `tools/package_firmware.py` accepte un dossier de build ESP-IDF déjà
vérifié, son `native-verification.json`, un profil normalisé exporté par la page,
et un SHA source complet. Il lit `flasher_args.json` pour les vrais offsets,
vérifie les parties non vides, leur non-chevauchement, les tailles flash, le hash
et la taille de l'application, puis produit des bins, un manifest ESP Web Tools,
une provenance et une entrée à incorporer manuellement dans le catalogue.

Exemple (chemins fournis par une CI contrôlée) :

```bash
python3 tools/package_firmware.py \
  --build /tmp/ug-ci-a/build \
  --verified /tmp/ug-ci-a/native-verification.json \
  --profile-json /tmp/config.json \
  --source-sha 0123456789012345678901234567890123456789 \
  --output docs/flasher
```

La page relit le manifeste et la provenance, puis vérifie en navigateur les SHA-256 de tous les segments avant d’activer ESP Web Tools. Ces contrôles vérifient la cohérence des octets avec le catalogue publié par le dépôt ; ce n’est pas une attestation de matériel ni une signature indépendante.

Le script **ne vérifie pas encore indépendamment** dans `sdkconfig` que la MAC,
le canal, la broche UART et le débit compilés correspondent au JSON. Cette
vérification est indispensable dans la CI AVANT publication ; elle n'est pas
annoncée comme acquise. Le script ne déclenche aucun build à la demande.
