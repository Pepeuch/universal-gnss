/* SPDX-License-Identifier: GPL-3.0-only
 * Données du configurateur UG. Les dessins ESPBoards sont des ressources
 * distinctes sous CC BY-NC 4.0, inchangées dans flasher/assets/boards/.
 * Les fonctions "raccordé" ne signifient pas "périphérique activé".
 */
(() => {
  'use strict';
  const uart = ['TX','RX','RTS','CTS'];
  const spi = ['SCK','MOSI','MISO','CS0','CS1','CS2'];
  const i2c = ['SDA','SCL'];
  const general = ['GNSS_PPS','INT','AUX1','AUX2','PWM'];
  const standardBuses = (withI2c1=false, withUart2=false, withSpi3=false) => [
    {id:'UART0',kind:'UART',signals:uart},
    {id:'UART1',kind:'UART',signals:uart},
    ...(withUart2?[{id:'UART2',kind:'UART',signals:uart}]:[]),
    {id:'SPI2',kind:'SPI',signals:spi},
    ...(withSpi3?[{id:'SPI3',kind:'SPI',signals:spi}]:[]),
    {id:'I2C0',kind:'I2C',signals:i2c},
    ...(withI2c1?[{id:'I2C1',kind:'I2C',signals:i2c}]:[]),
    {id:'GPIO',kind:'GPIO',signals:general}
  ];
  const boards = [
    {
      id:'s3-gateway',label:'Waveshare ESP32-S3-ETH',chip:'ESP32-S3',
      firmwareDevice:'s3-gateway', status:'ready',
      svg:'flasher/assets/boards/waveshare-esp32-s3-eth-pinout-high-resolution.svg',
      svgViewBox:'40 157 1333.097 889.843',
      sourceUrl:'https://www.espboards.dev/esp32/waveshare-esp32-s3-eth/',
      description:'Profil ESP32-S3-ETH : mémoires et USB protégés ; caméra et microSD physiquement raccordées même si non activées.',
      pins:[0,1,2,3,15,16,17,18,19,20,21,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48],
      locked:{19:'USB D− raccordé au connecteur',20:'USB D+ raccordé au connecteur',
        33:'PSRAM octale',34:'PSRAM octale',35:'PSRAM octale',36:'PSRAM octale',37:'PSRAM octale'},
      inputOnly:[46],extraPads:{},
      warnings:{
        0:'BOOT / strapping',3:'Strapping et XCLK caméra sur connecteur',
        1:'Caméra VSYNC si caméra branchée',2:'Caméra HREF si caméra branchée',
        15:'Caméra D6 si caméra branchée',18:'Caméra D7 si caméra branchée',
        21:'LED RGB WS2812 intégrée',38:'Caméra D5 si caméra branchée',
        39:'Caméra PCLK / JTAG',40:'Caméra D4 / JTAG',41:'Caméra D0 / JTAG',42:'Caméra D3 / JTAG',
        43:'UART0 TX / messages de démarrage',44:'UART0 RX / console',
        45:'Strapping + caméra D1',46:'Entrée seule, strapping + caméra D2',
        47:'Caméra SIOC si caméra branchée',48:'Caméra SIOD si caméra branchée'
      },
      buses: standardBuses(true,true,true).map(bus=>bus.id==='SPI2'
        ?{...bus,locked:'Réservé au W5500 dans le profil UG (contrôleur à vérifier dans le firmware)'}:bus),
      gnssUarts:['UART0','UART1','UART2'],defaultGnssUart:'UART1',
      fixedGroups:[
        {title:'Mémoire Flash / PSRAM',state:'Réservé système',summary:'SPI0/SPI1 (mémoire, non assignables)',
          pins:'PSRAM GPIO33–37 ; autres lignes mémoire internes/non exposées',open:true},
        {title:'Ethernet W5500',state:'Câblé sur carte',summary:'SPI : MOSI 11 · MISO 12 · SCK 13 · CS 14 · RST 9 · INT 10',
          pins:'GPIO9–14 (non exposés sur le connecteur 40 points)',open:true},
        {title:'MicroSD',state:'Câblé, activation optionnelle',summary:'CS 4 · MISO 5 · MOSI 6 · SCK 7',
          pins:'GPIO4–7 (non exposés sur le connecteur)'},
        {title:'USB natif',state:'Réservé',summary:'D− GPIO19 · D+ GPIO20',pins:'GPIO19, GPIO20'},
        {title:'Caméra DVP',state:'Connecteur présent, caméra optionnelle',
          summary:'PWR 8 · VSYNC 1 · HREF 2 · XCLK 3 · PCLK 39 · SIOD 48 · SIOC 47',
          pins:'D0 41 · D1 45 · D2 46 · D3 42 · D4 40 · D5 38 · D6 15 · D7 18'},
        {title:'LED RGB',state:'Câblé sur carte',summary:'WS2812 GPIO21',pins:'GPIO21'}
      ]
    },
    {
      // La carte C3 Plus est distincte du "supermini-c3" historique du firmware.
      // Ne pas activer le flash automatique avant comparaison physique et qualification.
      id:'c3-super-mini-plus',label:'ESP32-C3 Super Mini Plus',chip:'ESP32-C3',
      firmwareDevice:null,status:'pinout-ready-flash-pending',
      svg:'flasher/assets/boards/esp32-c3-super-mini-plus-pinout-high-resolution.svg',
      svgViewBox:'40 157 1545.196 305.307',
      sourceUrl:'https://www.espboards.dev/esp32/esp32-c3-super-mini-plus/',
      description:'SVG C3 Super Mini Plus fourni. GPIO cliquables ; le firmware C3 historique n’est pas automatiquement déclaré compatible.',
      pins:[0,1,2,3,4,5,6,7,8,9,10,20,21],
      locked:{},inputOnly:[],extraPads:{},
      warnings:{2:'Strapping : précaution au démarrage',8:'Strapping / signal SDA indicatif sur le dessin',
        9:'Strapping / signal SCL indicatif sur le dessin',
        20:'UART0 RX / console potentielle',21:'UART0 TX / console potentielle'},
      buses:standardBuses(),gnssUarts:['UART0','UART1'],defaultGnssUart:'UART1',
      fixedGroups:[
        {title:'UART0',state:'Fonction native / à vérifier selon firmware',summary:'RX GPIO20 · TX GPIO21',pins:'Broches éventuellement utilisables avec précaution'},
        {title:'Broches de démarrage',state:'Sensibles',summary:'GPIO2, GPIO8, GPIO9 (strapping)',pins:'Vérifier les niveaux au démarrage'}
      ]
    },
    {
      id:'c6-super-mini',label:'ESP32-C6 Super Mini',chip:'ESP32-C6',
      firmwareDevice:null,status:'pinout-ready-flash-pending',
      svg:'flasher/assets/boards/esp32-c6-super-mini-pinout-high-resolution.svg',
      svgViewBox:'40 145 1542.362 765',
      sourceUrl:'https://www.espboards.dev/esp32/esp32-c6-super-mini/',
      description:'Vue avant/arrière du C6 Super Mini. Les pastilles de soudure arrière sont indiquées ; aucun firmware C6 disponible.',
      pins:[0,1,2,3,4,5,6,7,8,9,12,13,14,15,16,17,18,19,20,21,22,23],
      extraPads:{CO2:21,CO1:22,CO0:23,CO3:12,CO4:13},
      locked:{12:'USB natif D−',13:'USB natif D+'},inputOnly:[],
      warnings:{4:'Strapping ESP32-C6',5:'Strapping ESP32-C6',8:'Strapping + RGB embarqué',
        9:'BOOT / strapping',15:'Strapping + LED utilisateur',
        16:'UART0 TX / console potentielle',17:'UART0 RX / console potentielle',
        21:'Pastille de soudure arrière, accès difficile',22:'Pastille de soudure arrière, accès difficile',
        23:'Pastille de soudure arrière, accès difficile'},
      buses:standardBuses(),gnssUarts:['UART0','UART1'],defaultGnssUart:'UART1',
      fixedGroups:[
        {title:'USB natif',state:'Réservé',summary:'D− GPIO12 · D+ GPIO13',pins:'Pastilles visibles au dos'},
        {title:'LEDs embarquées',state:'Câblé sur carte',summary:'RGB GPIO8 · statut GPIO15',pins:'GPIO8/15 : attention aux niveaux de boot'},
        {title:'BOOT / strapping',state:'Sensibles',summary:'GPIO4, 5, 8, 9, 15',pins:'GPIO9 : bouton BOOT'},
        {title:'UART0',state:'Fonction usuelle',summary:'TX GPIO16 · RX GPIO17',pins:'À confirmer selon configuration de console'},
        {title:'Pastilles supplémentaires',state:'Au dos',summary:'GPIO21, GPIO22, GPIO23',pins:'Pastilles à souder, pas des broches de connecteur'}
      ]
    },
    {
      id:'c3-rover-usb',label:'ESP32-C3 SuperMini (ancien modèle UG, SVG non fourni)',chip:'ESP32-C3',
      firmwareDevice:'c3-rover-usb',status:'awaiting-pinout',svg:null,sourceUrl:null,svgViewBox:null,
      description:'Profil historique du firmware C3 conservé, mais aucun SVG du modèle exact n’a été fourni.',
      pins:[],locked:{},warnings:{},inputOnly:[],extraPads:{},
      buses:standardBuses(),gnssUarts:['UART0','UART1'],defaultGnssUart:'UART1',fixedGroups:[]
    }
  ];
  window.UGPinBoards=Object.freeze(boards);
})();
