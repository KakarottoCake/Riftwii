#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The menu's translations, kept in one table and written out as the
wii/lang/<lang>.po files the build embeds (wii/i18n.cpp).

    python tools/lang_source.py

English is the msgid: the text exactly as the code has it. {1}, {2}...
are filled in by the menu and may move within a translation. A missing
entry shows in English. Players can override any of them with
sd:/riftwii/lang/<lang>.po (same format).
"""

import os

LANGS = ["es", "ja", "pt", "it"]
NAMES = {"es": "Spanish", "ja": "Japanese", "pt": "Portuguese", "it": "Italian"}

# msgid: (es, ja, pt, it)
T = {
    # Home
    "Games with mods": ("Juegos con mods", "MODがあるゲーム", "Jogos com mods", "Giochi con mod"),
    "All games": ("Todos los juegos", "すべてのゲーム", "Todos os jogos", "Tutti i giochi"),
    "Recently played": ("Jugados hace poco", "最近遊んだゲーム", "Jogados recentemente", "Giocati di recente"),
    "No game on these drives was played from RiftWii yet. Press 1 for all games.": (
        "Aún no se ha jugado desde RiftWii a ningún juego de estas unidades. Pulsa 1 para ver todos.",
        "これらのドライブのゲームはまだRiftWiiで遊んでいません。1ですべてのゲームを表示します。",
        "Nenhum jogo destas unidades foi jogado pelo RiftWii ainda. Aperte 1 para ver todos.",
        "Nessun gioco di queste unità è stato ancora giocato da RiftWii. Premi 1 per vederli tutti."),
    # {1} month, {2} day.
    "{1}/{2}": ("{2}/{1}", "{1}/{2}", "{2}/{1}", "{2}/{1}"),
    "Played once, on {1}": ("Jugado una vez, el {1}", "{1}に1回遊びました", "Jogado uma vez, em {1}", "Giocato una volta, il {1}"),
    "Played {1} times, last on {2}": ("Jugado {1} veces, la última el {2}", "{1}回遊びました (最後は{2})",
                                      "Jogado {1} vezes, a última em {2}", "Giocato {1} volte, l'ultima il {2}"),
    "1: view   2: settings   -/+: pages   B: A to Z": (
        "1: vista   2: ajustes   -/+: páginas   B: A a Z",
        "1: 表示   2: 設定   -/+: ページ   B: A〜Z",
        "1: visão   2: configurações   -/+: páginas   B: A a Z",
        "1: vista   2: impostazioni   -/+: pagine   B: dalla A alla Z"),
    "Page {1} of {2}": ("Página {1} de {2}", "{1} / {2} ページ", "Página {1} de {2}", "Pagina {1} di {2}"),
    "{1}: no games in /wbfs or /games": ("{1}: no hay juegos en /wbfs ni en /games", "{1}: /wbfs と /games にゲームがありません",
                                         "{1}: nenhum jogo em /wbfs ou /games", "{1}: nessun gioco in /wbfs o /games"),
    "SD: no card": ("SD: sin tarjeta", "SD: カードがありません", "SD: sem cartão", "SD: nessuna scheda"),
    "No d2x cIOS in 249-251: games cannot boot yet": (
        "No hay cIOS d2x en 249-251: los juegos aún no pueden arrancar",
        "249〜251にd2x cIOSがありません: まだゲームを起動できません",
        "Nenhum cIOS d2x em 249-251: os jogos ainda não podem iniciar",
        "Nessun cIOS d2x in 249-251: i giochi non possono ancora partire"),
    "No game here has packs in sd:/riivolution yet. Press 1 for all games.": (
        "Ningún juego tiene packs en sd:/riivolution todavía. Pulsa 1 para ver todos.",
        "sd:/riivolution にパックのあるゲームはまだありません。1ですべてのゲームを表示します。",
        "Nenhum jogo tem packs em sd:/riivolution ainda. Aperte 1 para ver todos.",
        "Nessun gioco ha ancora pacchetti in sd:/riivolution. Premi 1 per vederli tutti."),
    "No games found (usb:/wbfs, usb:/games, sd:/wbfs, sd:/games)": (
        "No se encontraron juegos (usb:/wbfs, usb:/games, sd:/wbfs, sd:/games)",
        "ゲームが見つかりません (usb:/wbfs, usb:/games, sd:/wbfs, sd:/games)",
        "Nenhum jogo encontrado (usb:/wbfs, usb:/games, sd:/wbfs, sd:/games)",
        "Nessun gioco trovato (usb:/wbfs, usb:/games, sd:/wbfs, sd:/games)"),
    "Reading the SD card...": ("Leyendo la tarjeta SD...", "SDカードを読み込んでいます...", "Lendo o cartão SD...",
                               "Lettura della scheda SD..."),
    "Reading the USB drive... (a big drive takes a moment)": (
        "Leyendo la unidad USB... (una unidad grande tarda un poco)",
        "USBドライブを読み込んでいます...(大きなドライブは少し時間がかかります)",
        "Lendo a unidade USB... (uma unidade grande demora um pouco)",
        "Lettura dell'unità USB... (un'unità grande richiede un momento)"),
    "Reading the disc...": ("Leyendo el disco...", "ディスクを読み込んでいます...", "Lendo o disco...", "Lettura del disco..."),
    "scan failed": ("error al buscar", "検索に失敗しました", "falha na busca", "ricerca non riuscita"),
    "(the menu runs under IOS {1}; USB drives need a base-58 cIOS for that, or set the menu IOS back to 58)": (
        "(el menú usa el IOS {1}; para eso las unidades USB necesitan un cIOS con base 58, o vuelve a poner el IOS del menú en 58)",
        "(メニューはIOS {1}で動作中です。USBドライブにはベース58のcIOSが必要です。またはメニューIOSを58に戻してください)",
        "(o menu usa o IOS {1}; para isso as unidades USB precisam de um cIOS com base 58, ou volte o IOS do menu para 58)",
        "(il menu usa l'IOS {1}; per questo le unità USB richiedono un cIOS con base 58, oppure riporta l'IOS del menu a 58)"),
    "(after the failed launch RiftWii came back under IOS {1}, which cannot read the drive here; start RiftWii again from the Homebrew Channel)": (
        "(tras el inicio fallido RiftWii volvió con el IOS {1}, que aquí no puede leer la unidad; vuelve a iniciar RiftWii desde el Homebrew Channel)",
        "(起動に失敗した後、RiftWiiはIOS {1}で戻りました。このIOSではドライブを読めません。Homebrew ChannelからRiftWiiを起動し直してください)",
        "(depois da falha ao iniciar, o RiftWii voltou com o IOS {1}, que aqui não lê a unidade; inicie o RiftWii de novo pelo Homebrew Channel)",
        "(dopo l'avvio non riuscito RiftWii è tornato con l'IOS {1}, che qui non legge l'unità; riavvia RiftWii dall'Homebrew Channel)"),
    "No disc in the drive": ("No hay disco en la unidad", "ディスクが入っていません", "Nenhum disco na unidade",
                             "Nessun disco nell'unità"),
    "Disc drive": ("Lector de discos", "ディスクドライブ", "Leitor de discos", "Lettore di dischi"),
    "USB drive": ("Unidad USB", "USBドライブ", "Unidade USB", "Unità USB"),
    "SD card": ("Tarjeta SD", "SDカード", "Cartão SD", "Scheda SD"),
    "Sun": ("Dom", "日", "Dom", "Dom"),
    "Mon": ("Lun", "月", "Seg", "Lun"),
    "Tue": ("Mar", "火", "Ter", "Mar"),
    "Wed": ("Mié", "水", "Qua", "Mer"),
    "Thu": ("Jue", "木", "Qui", "Gio"),
    "Fri": ("Vie", "金", "Sex", "Ven"),
    "Sat": ("Sáb", "土", "Sáb", "Sab"),
    # {1} 12-hour hour, {2} minutes, {3} 24-hour hour.
    "{1}:{2} AM": ("{1}:{2} a. m.", "午前 {1}:{2}", "{3}:{2}", "{3}:{2}"),
    "{1}:{2} PM": ("{1}:{2} p. m.", "午後 {1}:{2}", "{3}:{2}", "{3}:{2}"),
    "{1} {2}/{3}": ("{1} {3}/{2}", "{2}/{3} ({1})", "{1} {3}/{2}", "{1} {3}/{2}"),
    "MODS": ("MODS", "MOD", "MODS", "MOD"),

    # Game page
    "Back": ("Atrás", "もどる", "Voltar", "Indietro"),
    "Start": ("Jugar", "はじめる", "Jogar", "Gioca"),
    "Saves": ("Partidas", "セーブ", "Saves", "Salvataggi"),
    "On the Wii": ("En la Wii", "Wii本体", "No Wii", "Sulla Wii"),
    "SD, from Wii save": ("SD, desde la de la Wii", "SD (Wiiのセーブから)", "SD, a partir do save do Wii", "SD, dal salvataggio Wii"),
    "SD, fresh start": ("SD, empezar de cero", "SD (最初から)", "SD, do zero", "SD, da zero"),
    "Kept by the pack": ("Las gestiona el pack", "パックが管理", "Controlados pelo pack", "Gestiti dal pacchetto"),
    "This pack keeps its own saves. Turn it off to choose here.": (
        "Este pack guarda sus propias partidas. Desactívalo para elegir aquí.",
        "このパックは独自のセーブを使います。ここで選ぶにはパックをオフにしてください。",
        "Este pack guarda seus próprios saves. Desative-o para escolher aqui.",
        "Questo pacchetto ha i propri salvataggi. Disattivalo per scegliere qui."),
    "Saves go to the SD card, starting from the Wii's save.": (
        "Las partidas se guardan en la SD, empezando por la de la Wii.",
        "セーブはSDカードに保存されます(Wiiのセーブから始めます)。",
        "Os saves vão para o cartão SD, começando pelo save do Wii.",
        "I salvataggi vanno sulla scheda SD, partendo da quello della Wii."),
    "Saves go to the SD card, starting fresh.": (
        "Las partidas se guardan en la SD, empezando de cero.",
        "セーブはSDカードに保存されます(最初から始めます)。",
        "Os saves vão para o cartão SD, começando do zero.",
        "I salvataggi vanno sulla scheda SD, partendo da zero."),
    "Saves stay on the Wii, as usual.": ("Las partidas se quedan en la Wii, como siempre.",
                                         "セーブはいつも通りWii本体に保存されます。",
                                         "Os saves ficam no Wii, como sempre.",
                                         "I salvataggi restano sulla Wii, come sempre."),
    "Broken": ("Dañado", "エラー", "Com erro", "Danneggiato"),
    "On": ("Sí", "オン", "Sim", "Sì"),
    "Off": ("No", "オフ", "Não", "No"),
    "Mods": ("Mods", "MOD", "Mods", "Mod"),
    "None": ("Ninguno", "なし", "Nenhum", "Nessuno"),
    "{1} switched on": ("{1} activados", "{1}個オン", "{1} ativados", "{1} attivi"),
    "On: {1}": ("Activados: {1}", "オン: {1}", "Ativados: {1}", "Attivi: {1}"),
    "No mods for this game.": ("No hay mods para este juego.", "このゲームのMODはありません。", "Nenhum mod para este jogo.",
                               "Nessuna mod per questo gioco."),
    "No mods on the SD card. Put Riivolution XML in sd:/riivolution.": (
        "No hay mods en la tarjeta SD. Pon los XML de Riivolution en sd:/riivolution.",
        "SDカードにMODがありません。RiivolutionのXMLを sd:/riivolution に入れてください。",
        "Nenhum mod no cartão SD. Coloque os XML do Riivolution em sd:/riivolution.",
        "Nessuna mod sulla scheda SD. Metti gli XML di Riivolution in sd:/riivolution."),
    "1 mod pack for this game. Press A to turn it on.": (
        "1 paquete de mods para este juego. Pulsa A para activarlo.",
        "このゲームのMODパックが1個あります。Aでオンにできます。",
        "1 pacote de mods para este jogo. Aperte A para ativá-lo.",
        "1 pacchetto di mod per questo gioco. Premi A per attivarlo."),
    "{1} mod packs for this game. Press A to turn them on.": (
        "{1} paquetes de mods para este juego. Pulsa A para activarlos.",
        "このゲームのMODパックが{1}個あります。Aでオンにできます。",
        "{1} pacotes de mods para este jogo. Aperte A para ativá-los.",
        "{1} pacchetti di mod per questo gioco. Premi A per attivarli."),
    "No mods on the SD card": ("No hay mods en la tarjeta SD", "SDカードにMODがありません", "Nenhum mod no cartão SD",
                               "Nessuna mod sulla scheda SD"),
    "No mods for this game": ("No hay mods para este juego", "このゲームのMODはありません", "Nenhum mod para este jogo",
                              "Nessuna mod per questo gioco"),
    "Put Riivolution XML in sd:/riivolution": ("Pon los XML de Riivolution en sd:/riivolution",
                                               "RiivolutionのXMLを sd:/riivolution に入れてください",
                                               "Coloque os XML do Riivolution em sd:/riivolution",
                                               "Metti gli XML di Riivolution in sd:/riivolution"),
    "1 XML file is for another game": (
        "1 archivo XML es de otro juego",
        "1個のXMLは他のゲーム用です",
        "1 arquivo XML é de outro jogo",
        "1 file XML è per un altro gioco"),
    "{1} XML files are for other games": (
        "{1} archivos XML son de otros juegos",
        "{1}個のXMLは他のゲーム用です",
        "{1} arquivos XML são de outros jogos",
        "{1} file XML sono per altri giochi"),
    "Package scan failed; go back and try again": ("Error al leer los packs; vuelve atrás e inténtalo de nuevo",
                                                   "パックの読み込みに失敗しました。もどってやり直してください",
                                                   "Falha ao ler os packs; volte e tente de novo",
                                                   "Lettura dei pacchetti non riuscita; torna indietro e riprova"),
    "This XML cannot be read; the error is listed under it.": (
        "Este XML no se puede leer; el error aparece debajo.",
        "このXMLは読み込めません。エラーは下に表示されています。",
        "Este XML não pode ser lido; o erro aparece abaixo.",
        "Questo XML non può essere letto; l'errore è indicato sotto."),
    "This XML cannot be read; fix it on the card and come back.": (
        "Este XML no se puede leer; corrígelo en la tarjeta y vuelve.",
        "このXMLは読み込めません。カード上で直してからもどってください。",
        "Este XML não pode ser lido; corrija-o no cartão e volte.",
        "Questo XML non può essere letto; correggilo sulla scheda e torna."),
    "This pack cannot be turned on.": ("Este pack no se puede activar.", "このパックはオンにできません。",
                                       "Este pack não pode ser ativado.", "Questo pacchetto non può essere attivato."),
    "Off. A turns it on.": ("Desactivado. A lo activa.", "オフ。Aでオンになります。", "Desativado. A ativa.",
                            "Disattivato. A lo attiva."),
    "Off. A turns it on and shows its setting.": ("Desactivado. A lo activa y muestra su opción.",
                                                  "オフ。Aでオンになり、設定が表示されます。",
                                                  "Desativado. A ativa e mostra sua opção.",
                                                  "Disattivato. A lo attiva e ne mostra l'opzione."),
    "Off. A turns it on and shows its {1} settings.": ("Desactivado. A lo activa y muestra sus {1} opciones.",
                                                       "オフ。Aでオンになり、{1}個の設定が表示されます。",
                                                       "Desativado. A ativa e mostra suas {1} opções.",
                                                       "Disattivato. A lo attiva e ne mostra le {1} opzioni."),
    "On. It applies as a whole.": ("Activado. Se aplica completo.", "オン。まとめて適用されます。",
                                   "Ativado. É aplicado por inteiro.", "Attivato. Si applica per intero."),
    "On, but nothing chosen yet: pick its settings below.": (
        "Activado, pero sin nada elegido: elige sus opciones abajo.",
        "オンですが、まだ何も選ばれていません。下で設定を選んでください。",
        "Ativado, mas nada escolhido ainda: escolha as opções abaixo.",
        "Attivato, ma non hai ancora scelto nulla: scegli le opzioni qui sotto."),
    "On, {1} of {2} settings chosen.": ("Activado, {1} de {2} opciones elegidas.", "オン。{2}個中{1}個の設定を選択中。",
                                        "Ativado, {1} de {2} opções escolhidas.", "Attivato, {1} di {2} opzioni scelte."),
    "No game is selected; go back and pick one.": ("No hay ningún juego elegido; vuelve y elige uno.",
                                                   "ゲームが選ばれていません。もどって選んでください。",
                                                   "Nenhum jogo escolhido; volte e escolha um.",
                                                   "Nessun gioco scelto; torna indietro e scegline uno."),
    "Preparing the mods...": ("Preparando los mods...", "MODを準備しています...", "Preparando os mods...",
                              "Preparazione delle mod..."),

    # Cheats and picture
    "Cheats": ("Trucos", "チート", "Trapaças", "Trucchi"),
    "Picture width": ("Ancho de imagen", "画面の幅", "Largura da imagem", "Larghezza immagine"),
    "Deflicker": ("Antiparpadeo", "ちらつき防止", "Antitremulação", "Antisfarfallio"),
    "Black borders": ("Bordes negros", "黒い枠", "Bordas pretas", "Bordi neri"),
    "Region video fix": ("Corrección de vídeo por región", "地域ビデオ修正", "Correção de vídeo por região", "Correzione video per regione"),
    "For a US or Japanese game that shows no picture on a console from another region: the game is told the video hardware matches its region.": (
        "Para un juego de EE. UU. o de Japón que no muestra imagen en una consola de otra región: al juego se le dice que el hardware de vídeo coincide con su región.",
        "他の地域の本体で映像が出ない北米版・日本版のゲーム向け：ビデオ機器がゲームの地域と同じだとゲームに伝えます。",
        "Para um jogo dos EUA ou do Japão que não mostra imagem num console de outra região: o jogo é informado de que o hardware de vídeo corresponde à sua região.",
        "Per un gioco americano o giapponese che non mostra immagini su una console di un'altra regione: al gioco viene detto che l'hardware video corrisponde alla sua regione."),
    "Framebuffer": ("Framebuffer", "フレームバッファ", "Framebuffer", "Framebuffer"),
    "704 pixels": ("704 píxeles", "704ピクセル", "704 pixels", "704 pixel"),
    "720 pixels (full)": ("720 píxeles (completo)", "720ピクセル (全体)", "720 pixels (total)", "720 pixel (pieno)"),
    "Game's own": ("El del juego", "ゲームのまま", "O do jogo", "Del gioco"),
    "Off (sharp)": ("No (nítido)", "オフ (くっきり)", "Não (nítido)", "No (nitido)"),
    "Low": ("Bajo", "弱", "Baixo", "Basso"),
    "Medium": ("Medio", "中", "Médio", "Medio"),
    "High": ("Alto", "強", "Alto", "Alto"),
    "Remove": ("Quitar", "なくす", "Remover", "Rimuovi"),
    "Keep": ("Dejar", "そのまま", "Manter", "Mantieni"),
    "Remove all (experimental)": ("Quitar todo (experimental)", "すべてなくす（実験的）", "Remover tudo (experimental)",
                                  "Rimuovi tutto (sperimentale)"),
    "Remove all": ("Quitar todo", "すべてなくす", "Remover tudo", "Rimuovi tutto"),
    "Default ({1})": ("Predeterminado ({1})", "標準 ({1})", "Padrão ({1})", "Predefinito ({1})"),
    "On, none picked": ("Sí, ninguno elegido", "オン (未選択)", "Sim, nenhuma escolhida", "Sì, nessuno scelto"),
    "On, {1} picked": ("Sí, {1} elegidos", "オン ({1}個)", "Sim, {1} escolhidas", "Sì, {1} scelti"),
    "Cheat codes for this game. Press A to choose them.": ("Códigos de trucos para este juego. Pulsa A para elegirlos.",
                                                           "このゲームのチートコードです。Aで選びます。",
                                                           "Códigos de trapaça deste jogo. Aperte A para escolhê-los.",
                                                           "Codici trucco per questo gioco. Premi A per sceglierli."),
    "How wide the picture is drawn. 720 fills the screen from side to side.": (
        "El ancho de la imagen. 720 llena la pantalla de lado a lado.",
        "画面の横幅です。720にすると画面の端から端まで表示されます。",
        "A largura da imagem. 720 preenche a tela de lado a lado.",
        "La larghezza dell'immagine. 720 riempie lo schermo da un lato all'altro."),
    "A filter that softens the picture to hide flicker. Off gives the sharpest picture.": (
        "Un filtro que suaviza la imagen para ocultar el parpadeo. Con No se ve más nítida.",
        "ちらつきを抑えるために画面をぼかすフィルターです。オフにすると一番くっきりします。",
        "Um filtro que suaviza a imagem para esconder a tremulação. Com Não ela fica mais nítida.",
        "Un filtro che ammorbidisce l'immagine per nascondere lo sfarfallio. Con No è più nitida."),
    "Remove stretches the picture over the bars at the sides. Remove all also stretches it over the bars at the top and bottom: experimental, some games show a broken picture or crash with it.": (
        "Quitar estira la imagen sobre las barras de los lados. Quitar todo también la estira sobre las barras de arriba y abajo: es experimental, con ello algunos juegos muestran una imagen rota o se cuelgan.",
        "「なくす」にすると左右の黒い帯の上まで画面を引き伸ばします。「すべてなくす」は上下の帯の上まで引き伸ばします。実験的な機能で、画面が崩れたり止まったりするゲームがあります。",
        "Remover estica a imagem sobre as barras dos lados. Remover tudo também a estica sobre as barras de cima e de baixo: é experimental, com isso alguns jogos mostram a imagem quebrada ou travam.",
        "Rimuovi allarga l'immagine sopra le bande ai lati. Rimuovi tutto la allarga anche sopra le bande in alto e in basso: è sperimentale, con questa opzione alcuni giochi mostrano un'immagine rovinata o si bloccano."),
    "Remove takes away the bars at the sides; Remove all also the top and bottom (experimental).": (
        "Quitar elimina las barras de los lados; Quitar todo también las de arriba y abajo (experimental).",
        "「なくす」は左右の帯を、「すべてなくす」は上下の帯も消します（実験的）。",
        "Remover tira as barras dos lados; Remover tudo também as de cima e de baixo (experimental).",
        "Rimuovi toglie le bande ai lati; Rimuovi tutto anche quelle in alto e in basso (sperimentale)."),
    "Last time, this game left black borders on all sides.": ("La última vez, este juego dejó bordes negros por todos lados.",
                                                              "前回、このゲームは上下左右に黒い枠がありました。",
                                                              "Da última vez, este jogo deixou bordas pretas em todos os lados.",
                                                              "L'ultima volta questo gioco ha lasciato bordi neri su tutti i lati."),
    "Last time, this game left black borders at the sides.": ("La última vez, este juego dejó bordes negros a los lados.",
                                                              "前回、このゲームは左右に黒い枠がありました。",
                                                              "Da última vez, este jogo deixou bordas pretas nas laterais.",
                                                              "L'ultima volta questo gioco ha lasciato bordi neri ai lati."),
    "Last time, this game left black borders at the top and bottom.": (
        "La última vez, este juego dejó bordes negros arriba y abajo.",
        "前回、このゲームは上下に黒い枠がありました。",
        "Da última vez, este jogo deixou bordas pretas em cima e embaixo.",
        "L'ultima volta questo gioco ha lasciato bordi neri sopra e sotto."),
    "Last time, this game filled the whole screen.": ("La última vez, este juego llenó toda la pantalla.",
                                                      "前回、このゲームは画面全体に表示されました。",
                                                      "Da última vez, este jogo preencheu a tela inteira.",
                                                      "L'ultima volta questo gioco ha riempito tutto lo schermo."),
    "Use cheats": ("Usar trucos", "チートを使う", "Usar trapaças", "Usa trucchi"),
    "Get the latest cheats": ("Descargar los últimos trucos", "最新のチートを取得", "Baixar as trapaças mais recentes",
                              "Scarica i trucchi più recenti"),
    "Download cheats": ("Descargar trucos", "チートをダウンロード", "Baixar trapaças", "Scarica trucchi"),
    "Download": ("Descargar", "ダウンロード", "Baixar", "Scarica"),
    "Edit first": ("Edítalo antes", "先に編集", "Edite antes", "Da modificare"),
    "The cheats are in {1}. Edit it on a computer to add your own.": (
        "Los trucos están en {1}. Edítalo en un ordenador para añadir los tuyos.",
        "チートは {1} にあります。パソコンで編集すると自分のコードを追加できます。",
        "As trapaças estão em {1}. Edite-o num computador para adicionar as suas.",
        "I trucchi sono in {1}. Modificalo su un computer per aggiungerne di tuoi."),
    "Downloading cheats...": ("Descargando trucos...", "チートをダウンロードしています...", "Baixando trapaças...",
                              "Download dei trucchi..."),
    "{1} cheats. Turn on the ones you want.": ("{1} trucos. Activa los que quieras.", "チートは{1}個です。使うものをオンにしてください。",
                                               "{1} trapaças. Ative as que quiser.", "{1} trucchi. Attiva quelli che vuoi."),
    "Could not download cheats: {1}": ("No se pudieron descargar los trucos: {1}", "チートをダウンロードできませんでした: {1}",
                                       "Não foi possível baixar as trapaças: {1}", "Impossibile scaricare i trucchi: {1}"),
    "No cheats found online for this game.": ("No se encontraron trucos en línea para este juego.",
                                              "このゲームのチートはオンラインで見つかりませんでした。",
                                              "Nenhuma trapaça encontrada online para este jogo.",
                                              "Nessun trucco trovato online per questo gioco."),
    "Cheats are only applied when this is On.": ("Los trucos solo se aplican si esto está en Sí.",
                                                 "これがオンのときだけチートが使われます。",
                                                 "As trapaças só são aplicadas quando isto está em Sim.",
                                                 "I trucchi si applicano solo quando questo è su Sì."),
    "Downloads are off in Settings.": ("Las descargas están desactivadas en Ajustes.", "設定でダウンロードがオフになっています。",
                                       "Os downloads estão desativados nas Configurações.",
                                       "I download sono disattivati nelle Impostazioni."),
    "No game is selected.": ("No hay ningún juego elegido.", "ゲームが選ばれていません。", "Nenhum jogo escolhido.",
                             "Nessun gioco scelto."),
    "No cheat file yet. Choose Download to get one.": ("Aún no hay archivo de trucos. Elige Descargar para obtenerlo.",
                                                       "チートファイルがまだありません。「ダウンロード」で取得できます。",
                                                       "Ainda não há arquivo de trapaças. Escolha Baixar para obter um.",
                                                       "Nessun file di trucchi. Scegli Scarica per ottenerne uno."),
    "No cheat file at {1}": (
        "No hay archivo de trucos en {1}",
        "{1} にチートファイルがありません",
        "Nenhum arquivo de trapaças em {1}",
        "Nessun file di trucchi in {1}"),
    "The cheat file has no cheats in it: {1}": ("El archivo de trucos no tiene trucos: {1}", "チートファイルにチートがありません: {1}",
                                                "O arquivo de trapaças está vazio: {1}", "Il file dei trucchi non ne contiene: {1}"),

    # Settings
    "Settings": ("Ajustes", "設定", "Configurações", "Impostazioni"),
    "Games": ("Juegos", "ゲーム", "Jogos", "Giochi"),
    "Menu": ("Menú", "メニュー", "Menu", "Menu"),
    "Finding games": ("Buscar juegos", "ゲームを探す", "Procurar jogos", "Trovare i giochi"),
    "Online": ("En línea", "オンライン", "Online", "Online"),
    "More": ("Más", "その他", "Mais", "Altro"),
    "Picture": ("Imagen", "画面", "Imagem", "Immagine"),
    "Other": ("Otros", "その他", "Outros", "Altro"),
    "Language": ("Idioma", "言語", "Idioma", "Lingua"),
    "Wii: {1}": ("Wii: {1}", "Wii: {1}", "Wii: {1}", "Wii: {1}"),
    "Download names and cheats": ("Descargar nombres y trucos", "ゲーム名とチートをダウンロード", "Baixar nomes e trapaças",
                                  "Scarica nomi e trucchi"),
    "Get the latest game names": ("Actualizar nombres de juegos", "最新のゲーム名を取得", "Atualizar nomes dos jogos",
                                  "Aggiorna i nomi dei giochi"),
    "Update": ("Actualizar", "更新", "Atualizar", "Aggiorna"),
    "Menu IOS": ("IOS del menú", "メニューのIOS", "IOS do menu", "IOS del menu"),
    "Menu IOS: IOS 58 (no d2x cIOS found)": ("IOS del menú: IOS 58 (no hay cIOS d2x)", "メニューのIOS: IOS 58 (d2x cIOSなし)",
                                             "IOS do menu: IOS 58 (nenhum cIOS d2x)", "IOS del menu: IOS 58 (nessun cIOS d2x)"),
    "Find network packs (RiiFS)": ("Buscar packs en red (RiiFS)", "ネットワークのパックを探す (RiiFS)",
                                   "Procurar packs na rede (RiiFS)", "Cerca pacchetti in rete (RiiFS)"),
    "Copy network packs again": ("Copiar de nuevo los packs de red", "ネットワークのパックをもう一度コピー",
                                 "Copiar de novo os packs da rede", "Copia di nuovo i pacchetti in rete"),
    "Resync": ("Resincronizar", "再同期", "Ressincronizar", "Risincronizza"),
    "Look for games again": ("Buscar juegos de nuevo", "ゲームをもう一度探す", "Procurar jogos de novo", "Cerca di nuovo i giochi"),
    "Rescan": ("Buscar", "再検索", "Procurar", "Cerca"),
    "Leave RiftWii": ("Salir de RiftWii", "RiftWiiを終わる", "Sair do RiftWii", "Esci da RiftWii"),
    "Exit": ("Salir", "終わる", "Sair", "Esci"),
    "These apply to every game. A game's own page can change them for that game.": (
        "Se aplican a todos los juegos. La página de cada juego puede cambiarlos para ese juego.",
        "すべてのゲームに適用されます。各ゲームのページでそのゲームだけ変えられます。",
        "Valem para todos os jogos. A página de cada jogo pode mudá-los só para ele.",
        "Valgono per tutti i giochi. La pagina di ogni gioco può cambiarle per quel gioco."),
    "Game names follow the language when they are downloaded.": (
        "Los nombres de los juegos siguen el idioma al descargarse.",
        "ゲーム名はダウンロード時にこの言語になります。",
        "Os nomes dos jogos seguem o idioma quando são baixados.",
        "I nomi dei giochi seguono la lingua quando vengono scaricati."),
    "Game names and cheats are downloaded when the Wii is online.": (
        "Los nombres y trucos se descargan cuando la Wii tiene conexión.",
        "Wiiがインターネットにつながっているとき、ゲーム名とチートをダウンロードします。",
        "Nomes e trapaças são baixados quando o Wii está online.",
        "Nomi e trucchi vengono scaricati quando la Wii è online."),
    "Nothing is downloaded. Names and cheats already on the card are still used.": (
        "No se descarga nada. Se siguen usando los nombres y trucos que ya están en la tarjeta.",
        "何もダウンロードしません。カードにあるゲーム名とチートはそのまま使います。",
        "Nada é baixado. Os nomes e trapaças que já estão no cartão continuam sendo usados.",
        "Non viene scaricato nulla. Nomi e trucchi già sulla scheda restano in uso."),
    "Downloads are off. Turn on Download names and cheats first.": (
        "Las descargas están desactivadas. Activa antes Descargar nombres y trucos.",
        "ダウンロードがオフです。先に「ゲーム名とチートをダウンロード」をオンにしてください。",
        "Os downloads estão desativados. Ative antes Baixar nomes e trapaças.",
        "I download sono disattivati. Attiva prima Scarica nomi e trucchi."),
    "Downloading game names...": ("Descargando nombres de juegos...", "ゲーム名をダウンロードしています...",
                                  "Baixando nomes dos jogos...", "Download dei nomi dei giochi..."),
    "Game names updated.": ("Nombres de juegos actualizados.", "ゲーム名を更新しました。", "Nomes dos jogos atualizados.",
                            "Nomi dei giochi aggiornati."),
    "Could not download game names: {1}": ("No se pudieron descargar los nombres: {1}", "ゲーム名をダウンロードできませんでした: {1}",
                                           "Não foi possível baixar os nomes: {1}", "Impossibile scaricare i nomi: {1}"),
    "Cannot write sd:/riftwii/settings.txt": ("No se puede escribir sd:/riftwii/settings.txt", "sd:/riftwii/settings.txt に書き込めません",
                                              "Não é possível gravar sd:/riftwii/settings.txt", "Impossibile scrivere sd:/riftwii/settings.txt"),
    "Cannot write sd:/riftwii/menu_ios.txt": ("No se puede escribir sd:/riftwii/menu_ios.txt", "sd:/riftwii/menu_ios.txt に書き込めません",
                                              "Não é possível gravar sd:/riftwii/menu_ios.txt", "Impossibile scrivere sd:/riftwii/menu_ios.txt"),
    "The menu runs under the Homebrew Channel's IOS (the default).": (
        "El menú usa el IOS del Homebrew Channel (el predeterminado).",
        "メニューはHomebrew ChannelのIOSで動作します (標準)。",
        "O menu usa o IOS do Homebrew Channel (o padrão).",
        "Il menu usa l'IOS dell'Homebrew Channel (predefinito)."),
    "The menu and every game run under cIOS {1}, so a cIOS with fakemote makes USB DS3/DS4 pads work as Wii Remotes. USB drives in the menu need a base-58 cIOS.": (
        "El menú y todos los juegos usan el cIOS {1}, así un cIOS con fakemote hace que los mandos USB DS3/DS4 funcionen como Wiimotes. Las unidades USB en el menú necesitan un cIOS con base 58.",
        "メニューとすべてのゲームがcIOS {1}で動作するので、fakemote入りのcIOSならUSBのDS3/DS4コントローラーをWiiリモコンとして使えます。メニューでUSBドライブを使うにはベース58のcIOSが必要です。",
        "O menu e todos os jogos usam o cIOS {1}, então um cIOS com fakemote faz controles USB DS3/DS4 funcionarem como Wii Remotes. Unidades USB no menu precisam de um cIOS com base 58.",
        "Il menu e tutti i giochi usano il cIOS {1}, quindi un cIOS con fakemote fa funzionare i controller USB DS3/DS4 come Wii Remote. Le unità USB nel menu richiedono un cIOS con base 58."),
    "Takes effect the next time RiftWii starts.": ("Se aplica la próxima vez que se abra RiftWii.",
                                                   "次にRiftWiiを起動したときに反映されます。",
                                                   "Vale a partir da próxima vez que o RiftWii abrir.",
                                                   "Ha effetto al prossimo avvio di RiftWii."),
    "Looks for a PC running a RiiFS server when the games are read. Rescan to look now.": (
        "Busca un PC con un servidor RiiFS al leer los juegos. Pulsa Buscar para hacerlo ahora.",
        "ゲームを読み込むときにRiiFSサーバーを動かしているPCを探します。今すぐ探すには再検索してください。",
        "Procura um PC com um servidor RiiFS ao ler os jogos. Use Procurar para fazer isso agora.",
        "Cerca un PC con un server RiiFS quando legge i giochi. Usa Cerca per farlo ora."),
    "Only servers named by <network> in an XML on the card are used.": (
        "Solo se usan los servidores indicados con <network> en un XML de la tarjeta.",
        "カード上のXMLの<network>で指定されたサーバーだけを使います。",
        "Só são usados os servidores indicados por <network> em um XML do cartão.",
        "Si usano solo i server indicati da <network> in un XML sulla scheda."),
    "The next launch copies every file of its network packs again.": (
        "El próximo inicio vuelve a copiar todos los archivos de sus packs de red.",
        "次の起動時に、ネットワークのパックのファイルをすべてコピーし直します。",
        "O próximo início copia de novo todos os arquivos dos packs da rede.",
        "Il prossimo avvio copia di nuovo tutti i file dei suoi pacchetti in rete."),

    # Launch
    "Starting": ("Iniciando", "起動中", "Iniciando", "Avvio di"),
    "Dumping files from": ("Copiando archivos de", "ファイルをコピー中", "Copiando arquivos de", "Copia dei file da"),
    "The game takes over the screen when it is ready.": ("El juego aparecerá en pantalla cuando esté listo.",
                                                         "準備ができるとゲームの画面に切り替わります。",
                                                         "O jogo aparece na tela quando estiver pronto.",
                                                         "Il gioco apparirà sullo schermo quando è pronto."),
    "Opening the game...": ("Abriendo el juego...", "ゲームを開いています...", "Abrindo o jogo...", "Apertura del gioco..."),
    # The GameCube adapter (Settings). "Tap" is its name in Japan.
    "GameCube adapter": ("Adaptador de GameCube", "GCコントローラ接続タップ", "Adaptador de GameCube",
                         "Adattatore GameCube"),
    "Check the GameCube adapter": ("Probar el adaptador de GameCube", "接続タップを確認",
                                   "Testar o adaptador de GameCube", "Prova l'adattatore GameCube"),
    "Test": ("Probar", "テスト", "Testar", "Prova"),
    # First-start tutorial
    "Welcome to RiftWii": ("Te damos la bienvenida a RiftWii", "RiftWiiへようこそ", "Boas-vindas ao RiftWii", "Benvenuto in RiftWii"),
    "RiftWii starts your Wii games with Riivolution-format mods, from the disc, a USB drive or the SD card. Your game files are never changed. This short tour shows the basics.": (
        "RiftWii inicia tus juegos de Wii con mods en formato Riivolution, desde el disco, una unidad USB o la tarjeta SD. Tus archivos de juego nunca se modifican. Este breve recorrido te enseña lo básico.",
        "RiftWiiは、ディスク、USBドライブ、SDカードのWiiゲームを、Riivolution形式のMODつきで起動します。ゲームのファイルは変更されません。このガイドで基本を紹介します。",
        "O RiftWii inicia seus jogos de Wii com mods no formato Riivolution, a partir do disco, de uma unidade USB ou do cartão SD. Os arquivos dos jogos nunca são alterados. Este breve tour mostra o básico.",
        "RiftWii avvia i tuoi giochi Wii con mod in formato Riivolution, dal disco, da un'unità USB o dalla scheda SD. I file dei giochi non vengono mai modificati. Questa breve guida mostra le basi."),
    "Your games": ("Tus juegos", "ゲーム", "Seus jogos", "I tuoi giochi"),
    "Put games in the wbfs or games folder at the top of the SD card or the USB drive (WBFS, ISO or RVZ). A disc in the drive shows up too. Home lists games that have mods first: press 1, or the round button at the bottom left, to see all your games.": (
        "Pon los juegos en la carpeta wbfs o games de la raíz de la tarjeta SD o de la unidad USB (WBFS, ISO o RVZ). También aparece el disco que esté en la unidad. Al principio se muestran los juegos con mods: pulsa 1, o el botón redondo de abajo a la izquierda, para ver todos.",
        "ゲームはSDカードかUSBドライブのwbfsまたはgamesフォルダに入れてください (WBFS、ISO、RVZ)。ドライブのディスクも表示されます。最初はMODがあるゲームだけが表示されます。すべてのゲームを見るには、1か左下の丸いボタンを押してください。",
        "Coloque os jogos na pasta wbfs ou games na raiz do cartão SD ou da unidade USB (WBFS, ISO ou RVZ). O disco na unidade também aparece. No início são mostrados os jogos com mods: aperte 1, ou o botão redondo embaixo à esquerda, para ver todos.",
        "Metti i giochi nella cartella wbfs o games nella radice della scheda SD o dell'unità USB (WBFS, ISO o RVZ). Appare anche il disco nell'unità. All'inizio vengono mostrati i giochi con mod: premi 1, o il pulsante rotondo in basso a sinistra, per vederli tutti."),
    "Put mod packs (the XML file and the folders that come with it) in sd:/riivolution or usb:/riivolution. Pick a game, open Mods, switch a pack on and choose its options. Start (or +) plays the game with them.": (
        "Pon los paquetes de mods (el archivo XML y las carpetas que lo acompañan) en sd:/riivolution o usb:/riivolution. Elige un juego, abre Mods, activa un paquete y elige sus opciones. Jugar (o +) inicia el juego con ellos.",
        "MODパック (XMLファイルと一緒のフォルダ) はsd:/riivolutionかusb:/riivolutionに入れてください。ゲームを選び、MODを開いてパックをオンにし、設定を選びます。はじめる (か+) でMODつきで遊べます。",
        "Coloque os pacotes de mods (o arquivo XML e as pastas que vêm com ele) em sd:/riivolution ou usb:/riivolution. Escolha um jogo, abra Mods, ative um pacote e escolha as opções. Jogar (ou +) inicia o jogo com eles.",
        "Metti i pacchetti di mod (il file XML e le cartelle che lo accompagnano) in sd:/riivolution o usb:/riivolution. Scegli un gioco, apri Mod, attiva un pacchetto e scegli le sue opzioni. Gioca (o +) avvia il gioco con le mod."),
    "Buttons": ("Botones", "ボタン", "Botões", "Pulsanti"),
    "Point with the Wii Remote and press A, or move with the D-pad; in the games list, - and + turn the pages. B goes back, 2 opens Settings and HOME opens the HOME Menu. The Classic Controller and GameCube controllers work too, with the same buttons.": (
        "Apunta con el mando de Wii y pulsa A, o muévete con la cruceta; en la lista de juegos, - y + pasan de página. B vuelve atrás, 2 abre los ajustes y HOME abre el menú HOME. El mando clásico y los mandos de GameCube también sirven, con los mismos botones.",
        "Wiiリモコンでポイントして A を押すか、十字ボタンで動かします。ゲーム一覧では - と + でページをめくります。B で戻り、2 で設定、HOME でHOMEメニューを開きます。クラシックコントローラとゲームキューブコントローラも同じボタンで使えます。",
        "Aponte com o Wii Remote e aperte A, ou mova com o direcional; na lista de jogos, - e + mudam de página. B volta, 2 abre as configurações e HOME abre o menu HOME. O Classic Controller e os controles de GameCube também funcionam, com os mesmos botões.",
        "Punta con il telecomando Wii e premi A, o muoviti con la croce direzionale; nell'elenco dei giochi, - e + cambiano pagina. B torna indietro, 2 apre le impostazioni e HOME apre il menu HOME. Anche il Classic Controller e i controller GameCube funzionano, con gli stessi pulsanti."),
    "You're all set": ("Todo listo", "準備完了", "Tudo pronto", "Tutto pronto"),
    "Settings has the video, language, online and update options. For more help, see the guide on RiftWii's GitHub page or join the Discord. Settings > Tutorial shows this tour again.": (
        "En los ajustes están las opciones de vídeo, idioma, juego en línea y actualizaciones. Para más ayuda, mira la guía en la página de GitHub de RiftWii o únete al Discord. Ajustes > Tutorial muestra este recorrido otra vez.",
        "設定には、映像、言語、オンライン、アップデートの設定があります。くわしくはRiftWiiのGitHubページのガイドか、Discordを見てください。設定 > チュートリアル でこのガイドをもう一度見られます。",
        "As configurações têm as opções de vídeo, idioma, jogo online e atualizações. Para mais ajuda, veja o guia na página do RiftWii no GitHub ou entre no Discord. Configurações > Tutorial mostra este tour de novo.",
        "Nelle impostazioni ci sono le opzioni di video, lingua, gioco online e aggiornamenti. Per altro aiuto, leggi la guida sulla pagina GitHub di RiftWii o entra nel Discord. Impostazioni > Tutorial mostra di nuovo questa guida."),
    "Next": ("Siguiente", "次へ", "Próximo", "Avanti"),
    "Skip": ("Omitir", "スキップ", "Pular", "Salta"),
    "Let's go": ("¡Vamos!", "はじめる", "Vamos lá", "Iniziamo"),
    "Tutorial": ("Tutorial", "チュートリアル", "Tutorial", "Tutorial"),
    "Show": ("Ver", "表示", "Ver", "Mostra"),
    "The short tour of RiftWii's basics that a new SD card starts with.": (
        "El breve recorrido por lo básico de RiftWii que aparece con una tarjeta SD nueva.",
        "新しいSDカードで最初に表示される、RiftWiiの基本の短いガイドです。",
        "O breve tour pelo básico do RiftWii que aparece com um cartão SD novo.",
        "La breve guida alle basi di RiftWii che appare con una nuova scheda SD."),
    "Credits and license": ("Créditos y licencia", "クレジットとライセンス", "Créditos e licença", "Riconoscimenti e licenza"),
    "View": ("Ver", "表示", "Ver", "Vedi"),
    "Clear search": ("Borrar búsqueda", "検索をやめる", "Limpar busca", "Annulla ricerca"),
    "No games found yet. Put your games (WBFS, ISO or RVZ) in a folder named wbfs or games at the top of the SD card or USB drive, then pick Look for games again in Settings.": (
        "Aún no hay juegos. Pon tus juegos (WBFS, ISO o RVZ) en una carpeta llamada wbfs o games en la raíz de la SD o del USB y luego elige Buscar juegos de nuevo en Ajustes.",
        "ゲームがまだありません。ゲーム(WBFS、ISO、RVZ)をSDカードかUSBドライブの一番上のwbfsまたはgamesフォルダに入れて、設定の「ゲームをもう一度探す」を選んでください。",
        "Nenhum jogo ainda. Coloque seus jogos (WBFS, ISO ou RVZ) numa pasta chamada wbfs ou games na raiz do SD ou do USB e depois escolha Procurar jogos de novo em Configurações.",
        "Ancora nessun gioco. Metti i tuoi giochi (WBFS, ISO o RVZ) in una cartella chiamata wbfs o games nella radice della SD o dell'unità USB, poi scegli Cerca di nuovo i giochi in Impostazioni."),
    "Who RiftWii's parts come from, its license (the GNU GPL, version 3 or later) and where its source is.": (
        "De quién vienen las partes de RiftWii, su licencia (la GNU GPL, versión 3 o posterior) y dónde está su código fuente.",
        "RiftWiiの各部分の作者、ライセンス (GNU GPL バージョン3以降) とソースコードの場所。",
        "De quem vêm as partes do RiftWii, sua licença (a GNU GPL, versão 3 ou posterior) e onde está seu código-fonte.",
        "Da chi provengono le parti di RiftWii, la sua licenza (la GNU GPL, versione 3 o successiva) e dove si trova il suo codice sorgente."),
    "Experimental. With the adapter plugged in when a game starts, its controllers fill the empty ports in games that take a GameCube controller. Needs IOS 58 or a d2x cIOS.": (
        "Experimental. Si el adaptador está conectado al iniciar un juego, sus mandos ocupan los puertos libres en los juegos que aceptan el mando de GameCube. Necesita IOS 58 o un cIOS d2x.",
        "試験的な機能です。ゲーム開始時に接続タップがつながっていれば、ゲームキューブコントローラ対応のゲームで、空いているポートに入ります。IOS 58かd2x cIOSが必要です。",
        "Experimental. Com o adaptador conectado quando um jogo começa, os controles dele ocupam as portas livres nos jogos que aceitam o controle de GameCube. Precisa do IOS 58 ou de um cIOS d2x.",
        "Sperimentale. Con l'adattatore collegato all'avvio di un gioco, i suoi controller occupano le porte libere nei giochi che accettano il controller GameCube. Serve l'IOS 58 o un cIOS d2x."),
    "Experimental. Always on, even with no adapter plugged in, so it can be plugged in during a game. It needs IOS 58 or a d2x cIOS.": (
        "Experimental. Siempre activo, aunque no haya adaptador, para poder conectarlo durante el juego. Necesita IOS 58 o un cIOS d2x.",
        "試験的な機能です。接続タップがなくても常に有効なので、ゲーム中につなぐこともできます。IOS 58かd2x cIOSが必要です。",
        "Experimental. Sempre ativo, mesmo sem adaptador, para poder conectá-lo durante o jogo. Precisa do IOS 58 ou de um cIOS d2x.",
        "Sperimentale. Sempre attivo, anche senza adattatore, così lo si può collegare durante il gioco. Serve l'IOS 58 o un cIOS d2x."),
    "Experimental. The adapter is left alone.": ("Experimental. El adaptador no se usa.", "試験的な機能です。接続タップは使いません。", "Experimental. O adaptador não é usado.",
                                   "Sperimentale. L'adattatore non viene usato."),
    "Adapter: working": ("Adaptador: funcionando", "接続タップ：動作中", "Adaptador: funcionando", "Adattatore: funziona"),
    "Adapter: starting...": ("Adaptador: iniciando...", "接続タップ：準備中...", "Adaptador: iniciando...",
                             "Adattatore: avvio..."),
    "Adapter: another program is using it, waiting": (
        "Adaptador: otro programa lo está usando, esperando",
        "接続タップ：ほかのプログラムが使っています。待っています",
        "Adaptador: outro programa está usando, aguardando",
        "Adattatore: lo usa un altro programma, in attesa"),
    # {1} IOS's error number.
    "Adapter: it did not answer ({1}), trying again": (
        "Adaptador: no respondió ({1}), reintentando",
        "接続タップ：応答がありません（{1}）。もう一度ためします",
        "Adaptador: não respondeu ({1}), tentando de novo",
        "Adattatore: nessuna risposta ({1}), nuovo tentativo"),
    "Adapter: not found. Plug in its black USB plug.": (
        "Adaptador: no encontrado. Conecta su enchufe USB negro.",
        "接続タップ：見つかりません。黒いUSBプラグをつないでください。",
        "Adaptador: não encontrado. Conecte o plugue USB preto.",
        "Adattatore: non trovato. Collega la spina USB nera."),
    "Press buttons on a controller in the adapter to see them here. In a game that supports the GameCube controller, the adapter's controllers fill the ports that have none plugged in.": (
        "Pulsa botones en un mando conectado al adaptador para verlos aquí. En un juego compatible con el mando de GameCube, los mandos del adaptador ocupan los puertos que no tienen ninguno conectado.",
        "接続タップにつないだコントローラのボタンを押すと、ここに表示されます。ゲームキューブコントローラに対応したゲームでは、何もつながっていないポートに接続タップのコントローラが入ります。",
        "Aperte botões em um controle ligado ao adaptador para vê-los aqui. Em um jogo compatível com o controle de GameCube, os controles do adaptador ocupam as portas sem nenhum conectado.",
        "Premi i pulsanti di un controller collegato all'adattatore per vederli qui. In un gioco che supporta il controller GameCube, i controller dell'adattatore occupano le porte senza nulla collegato."),
    # {1} the IOS number.
    "This IOS has no USB HID (IOS{1}). Choose IOS 58 or a d2x cIOS as the Menu IOS.": (
        "Este IOS no tiene USB HID (IOS{1}). Elige IOS 58 o un cIOS d2x como IOS del menú.",
        "このIOSにはUSB HIDがありません（IOS{1}）。メニューのIOSにIOS 58かd2x cIOSを選んでください。",
        "Este IOS não tem USB HID (IOS{1}). Escolha o IOS 58 ou um cIOS d2x como IOS do menu.",
        "Questo IOS non ha USB HID (IOS{1}). Scegli l'IOS 58 o un cIOS d2x come IOS del menu."),
    # {1} 1 to 4.
    "Port {1}": ("Puerto {1}", "ポート{1}", "Porta {1}", "Porta {1}"),
    "nothing plugged in": ("nada conectado", "未接続", "nada conectado", "niente collegato"),
    # Settings: what a row does, shown when it takes the focus.
    "The menu's language. Wii follows the console's own setting.": (
        "El idioma del menú. Wii sigue el ajuste de la consola.",
        "メニューの言語です。「Wii」は本体の設定に合わせます。",
        "O idioma do menu. Wii segue a configuração do console.",
        "La lingua del menu. Wii segue l'impostazione della console."),
    "Downloads the newest game names from GameTDB.": (
        "Descarga los nombres de juegos más recientes de GameTDB.",
        "GameTDBから最新のゲーム名をダウンロードします。",
        "Baixa os nomes de jogos mais recentes do GameTDB.",
        "Scarica i nomi dei giochi più recenti da GameTDB."),
    "Shows live what the controllers in the adapter are pressing.": (
        "Muestra en directo lo que pulsan los mandos del adaptador.",
        "接続タップのコントローラで押しているボタンをその場で表示します。",
        "Mostra ao vivo o que os controles do adaptador estão apertando.",
        "Mostra in tempo reale cosa premono i controller dell'adattatore."),
    "Reads the SD card and the USB drive again.": (
        "Vuelve a leer la tarjeta SD y la unidad USB.",
        "SDカードとUSBドライブをもう一度読み込みます。",
        "Lê o cartão SD e a unidade USB de novo.",
        "Rilegge la scheda SD e l'unità USB."),
    "No d2x cIOS was found in slots 248 to 252, so the menu runs under IOS 58. Install d2x to play games from SD or USB.": (
        "No se encontró ningún cIOS d2x en los slots 248 a 252, así que el menú usa el IOS 58. Instala d2x para jugar desde SD o USB.",
        "スロット248〜252にd2x cIOSがないため、メニューはIOS 58で動いています。SDやUSBのゲームを遊ぶにはd2xを入れてください。",
        "Nenhum cIOS d2x foi encontrado nos slots 248 a 252, então o menu usa o IOS 58. Instale o d2x para jogar pelo SD ou USB.",
        "Nessun cIOS d2x trovato negli slot da 248 a 252, quindi il menu usa l'IOS 58. Installa d2x per giocare da SD o USB."),
    # Update check
    "Check for a new version": ("Buscar una versión nueva", "新しいバージョンを確認", "Procurar uma versão nova", "Cerca una nuova versione"),
    "Check": ("Buscar", "確認", "Procurar", "Cerca"),
    "This is RiftWii {1}. Looks on GitHub for a newer release.": (
        "Esta es RiftWii {1}. Busca en GitHub una versión más reciente.",
        "これはRiftWii {1}です。GitHubで新しいリリースを探します。",
        "Esta é a RiftWii {1}. Procura no GitHub uma versão mais nova.",
        "Questa è RiftWii {1}. Cerca su GitHub una versione più recente."),
    "Asking GitHub...": ("Consultando GitHub...", "GitHubに問い合わせています...", "Consultando o GitHub...", "Chiedo a GitHub..."),
    "RiftWii {1} is out: {2}": ("Ya salió RiftWii {1}: {2}", "RiftWii {1}が出ています: {2}", "Saiu a RiftWii {1}: {2}", "È uscita RiftWii {1}: {2}"),
    "RiftWii {1} is the newest version.": ("RiftWii {1} es la versión más reciente.", "RiftWii {1}が最新バージョンです。",
                                           "A RiftWii {1} é a versão mais recente.", "RiftWii {1} è la versione più recente."),
    "Could not check: {1}": ("No se pudo comprobar: {1}", "確認できませんでした: {1}", "Não foi possível verificar: {1}",
                             "Impossibile controllare: {1}"),
    # Favourites
    "Favourites": ("Favoritos", "お気に入り", "Favoritos", "Preferiti"),
    "Favourite": ("Favorito", "お気に入り", "Favorito", "Preferito"),
    "Favourites have their own view on Home: press 1 there until it shows.": (
        "Los favoritos tienen su propia vista en Inicio: pulsa 1 allí hasta que aparezca.",
        "お気に入りはホームに専用の表示があります。出るまで1を押してください。",
        "Os favoritos têm sua própria visão no Início: aperte 1 lá até ela aparecer.",
        "I preferiti hanno una vista tutta loro nella Home: premi 1 finché non compare."),
    "No favourite is on these drives. Mark games on their page. Press 1 for all games.": (
        "No hay ningún favorito en estas unidades. Márcalos en la página del juego. Pulsa 1 para ver todos.",
        "これらのドライブにお気に入りはありません。ゲームのページで登録できます。1ですべてのゲームを表示します。",
        "Nenhum favorito nestas unidades. Marque os jogos na página deles. Aperte 1 para ver todos.",
        "Nessun preferito su queste unità. Segna i giochi nella loro pagina. Premi 1 per vederli tutti."),
    "Could not save the settings to the SD card.": (
        "No se pudieron guardar los ajustes en la tarjeta SD.",
        "設定をSDカードに保存できませんでした。",
        "Não foi possível salvar as configurações no cartão SD.",
        "Impossibile salvare le impostazioni sulla scheda SD."),
    # Video mode, game language, game cIOS
    "Video mode": ("Modo de vídeo", "映像モード", "Modo de vídeo", "Modalità video"),
    "Game language": ("Idioma del juego", "ゲームの言語", "Idioma do jogo", "Lingua del gioco"),
    "The console's": ("El de la consola", "本体の設定", "O do console", "Quella della console"),
    "Automatic": ("Automático", "自動", "Automático", "Automatico"),
    "Japanese": ("Japonés", "日本語", "Japonês", "Giapponese"),
    "English": ("Inglés", "英語", "Inglês", "Inglese"),
    "German": ("Alemán", "ドイツ語", "Alemão", "Tedesco"),
    "French": ("Francés", "フランス語", "Francês", "Francese"),
    "Spanish": ("Español", "スペイン語", "Espanhol", "Spagnolo"),
    "Italian": ("Italiano", "イタリア語", "Italiano", "Italiano"),
    "Dutch": ("Neerlandés", "オランダ語", "Holandês", "Olandese"),
    "Chinese (simplified)": ("Chino (simplificado)", "中国語 (簡体字)", "Chinês (simplificado)", "Cinese (semplificato)"),
    "Chinese (traditional)": ("Chino (tradicional)", "中国語 (繁体字)", "Chinês (tradicional)", "Cinese (tradizionale)"),
    "Korean": ("Coreano", "韓国語", "Coreano", "Coreano"),
    "The TV signal the game sends. PAL 50 Hz needs a TV that takes it, 480p a component cable.": (
        "La señal de TV que envía el juego. PAL 50 Hz necesita una TV que lo admita; 480p, un cable de componentes.",
        "ゲームが出す映像信号です。PAL 50 Hzには対応したテレビが、480pにはコンポーネントケーブルが必要です。",
        "O sinal de TV que o jogo envia. PAL 50 Hz precisa de uma TV compatível; 480p, de um cabo componente.",
        "Il segnale TV che il gioco invia. PAL 50 Hz richiede una TV che lo supporti, 480p un cavo component."),
    "The language the game is told the console uses. Pick one the game has: some games stop without it.": (
        "El idioma que el juego cree que usa la consola. Elige uno que el juego tenga: algunos se detienen sin él.",
        "ゲームに伝える本体の言語です。ゲームにある言語を選んでください。ないと止まるゲームもあります。",
        "O idioma que o jogo acha que o console usa. Escolha um que o jogo tenha: alguns jogos param sem ele.",
        "La lingua che il gioco crede usi la console. Scegline una che il gioco ha: alcuni giochi si bloccano senza."),
    "The d2x cIOS the game runs under. Automatic uses the menu's, else the first of 249, 250 and 251 that works.": (
        "El cIOS d2x con el que corre el juego. Automático usa el del menú o, si no, el primero de 249, 250 y 251 que funcione.",
        "ゲームを動かすd2x cIOSです。自動ではメニューのものを使い、なければ249、250、251のうち動くものを使います。",
        "O cIOS d2x em que o jogo roda. Automático usa o do menu ou, senão, o primeiro de 249, 250 e 251 que funcionar.",
        "Il cIOS d2x con cui gira il gioco. Automatico usa quello del menu, altrimenti il primo tra 249, 250 e 251 che funziona."),
    "Online server": ("Servidor en línea", "オンラインサーバー", "Servidor online", "Server online"),
    "Home tiles": ("Casillas de inicio", "ホームの表示", "Blocos do início", "Riquadri della home"),
    "Covers": ("Portadas", "パッケージ", "Capas", "Copertine"),
    "Names": ("Nombres", "名前", "Nomes", "Nomi"),
    "Shelf": ("Estante", "棚", "Estante", "Scaffale"),
    "Channels": ("Canales", "チャンネル", "Canais", "Canali"),
    "Widescreen menu": ("Menú panorámico", "ワイド画面メニュー", "Menu widescreen", "Menu panoramico"),
    "Screen size": ("Tamaño en pantalla", "画面の大きさ", "Tamanho na tela", "Dimensione sullo schermo"),
    "On a 16:9 TV the menu is drawn narrower, so covers and pictures keep their shape. Automatic follows the Wii's own TV setting.": (
        "En una TV 16:9 el menú se dibuja más estrecho, para que las portadas e imágenes conserven su forma. Automático sigue el ajuste de TV de la propia Wii.",
        "16:9のテレビではメニューを細く描き、パッケージや画像の形を保ちます。自動ではWii本体のテレビ設定に従います。",
        "Numa TV 16:9 o menu é desenhado mais estreito, para que as capas e imagens mantenham a forma. Automático segue o ajuste de TV do próprio Wii.",
        "Su una TV 16:9 il menu viene disegnato più stretto, così copertine e immagini mantengono la loro forma. Automatico segue l'impostazione TV della Wii."),
    "Makes the menu smaller on screen, so nothing is cut off at the TV's edges. Lower it until the whole menu shows.": (
        "Hace el menú más pequeño en pantalla, para que nada quede cortado en los bordes de la TV. Bájalo hasta que se vea todo el menú.",
        "画面上のメニューを小さくして、テレビの端で切れないようにします。メニュー全体が見えるまで下げてください。",
        "Deixa o menu menor na tela, para que nada seja cortado nas bordas da TV. Diminua até o menu inteiro aparecer.",
        "Rende il menu più piccolo sullo schermo, così niente viene tagliato ai bordi della TV. Abbassalo finché non vedi tutto il menu."),
    "Covers: each game's box art from GameTDB (fetched when downloads are on). Shelf: the boxes on a shelf. Channels: each game's animated icon, and its banner when you pick it, as on the Wii Menu. Names: the names only.": (
        "Portadas: la carátula de cada juego de GameTDB (se descarga si las descargas están activadas). Estante: las cajas en un estante. Canales: el icono animado de cada juego, y su banner al elegirlo, como en el menú de Wii. Nombres: solo los nombres.",
        "パッケージ: GameTDBの各ゲームのパッケージ画像(ダウンロードがオンのとき取得)。棚: ケースを棚に並べます。チャンネル: Wiiメニューのように各ゲームの動くアイコンと、選んだときのバナー。名前: 名前だけ。",
        "Capas: a capa de cada jogo do GameTDB (baixada quando os downloads estão ligados). Estante: as caixas numa estante. Canais: o ícone animado de cada jogo, e o banner ao escolhê-lo, como no Menu Wii. Nomes: só os nomes.",
        "Copertine: la copertina di ogni gioco da GameTDB (scaricata se i download sono attivi). Scaffale: le custodie su uno scaffale. Canali: l'icona animata di ogni gioco, e il banner quando lo scegli, come nel Menu Wii. Nomi: solo i nomi."),
    "Custom (no wfc_domain set)": (
        "Personalizado (sin wfc_domain)", "カスタム（wfc_domain未設定）", "Personalizado (sem wfc_domain)",
        "Personalizzato (wfc_domain non impostato)"),
    "The online server the game uses in place of Nintendo's, which closed. Custom uses wfc_domain in settings.txt.": (
        "El servidor en línea que usa el juego en lugar del de Nintendo, que cerró. Personalizado usa wfc_domain de settings.txt.",
        "終了したNintendoのサーバーの代わりにゲームが使うオンラインサーバーです。カスタムはsettings.txtのwfc_domainを使います。",
        "O servidor online que o jogo usa no lugar do da Nintendo, que foi desligado. Personalizado usa wfc_domain do settings.txt.",
        "Il server online che il gioco usa al posto di quello di Nintendo, ormai chiuso. Personalizzato usa wfc_domain in settings.txt."),
    # Covers (Home's status line and the game page's Cover row)
    "Getting covers from GameTDB: {1} left": (
        "Descargando portadas de GameTDB: faltan {1}",
        "GameTDBからカバーを取得中: 残り{1}",
        "Baixando capas do GameTDB: faltam {1}",
        "Scaricamento copertine da GameTDB: ne mancano {1}"),
    "Covers could not be downloaded ({1}). To try again, use Settings > Look for games again.": (
        "No se pudieron descargar las portadas ({1}). Para volver a intentarlo, usa Ajustes > Buscar juegos de nuevo.",
        "カバーをダウンロードできませんでした ({1})。設定 > ゲームをもう一度探す でもう一度試せます。",
        "Não foi possível baixar as capas ({1}). Para tentar de novo, use Configurações > Procurar jogos de novo.",
        "Impossibile scaricare le copertine ({1}). Per riprovare, usa Impostazioni > Cerca di nuovo i giochi."),
    "Cover": ("Portada", "カバー", "Capa", "Copertina"),
    "Download again": ("Volver a descargar", "もう一度ダウンロード", "Baixar de novo", "Scarica di nuovo"),
    "Downloads this game's box art from GameTDB now.": (
        "Descarga ahora la portada de este juego desde GameTDB.",
        "このゲームのカバーを今すぐGameTDBからダウンロードします。",
        "Baixa agora a capa deste jogo do GameTDB.",
        "Scarica ora la copertina di questo gioco da GameTDB."),
    "Downloading the cover...": ("Descargando la portada...", "カバーをダウンロード中...", "Baixando a capa...", "Scaricamento della copertina..."),
    "Cover downloaded.": ("Portada descargada.", "カバーをダウンロードしました。", "Capa baixada.", "Copertina scaricata."),
    "GameTDB has no cover for this game.": (
        "GameTDB no tiene portada para este juego.",
        "GameTDBにはこのゲームのカバーがありません。",
        "O GameTDB não tem capa para este jogo.",
        "GameTDB non ha una copertina per questo gioco."),
    "Could not download the cover: {1}": (
        "No se pudo descargar la portada: {1}",
        "カバーをダウンロードできませんでした: {1}",
        "Não foi possível baixar a capa: {1}",
        "Impossibile scaricare la copertina: {1}"),
    # Menu sounds (Settings)
    "Menu sounds": ("Sonidos del menú", "メニューの音", "Sons do menu", "Suoni del menu"),
    "Normal": ("Normal", "標準", "Normal", "Normale"),
    "Quiet": ("Bajo", "小さめ", "Baixo", "Bassi"),
    "How loud the menu's clicks are. Quiet softens the tick the pointer makes moving onto something.": (
        "El volumen de los clics del menú. Bajo suaviza el sonido al pasar el puntero sobre algo.",
        "メニューのクリック音の大きさです。小さめにすると、ポインターが何かに重なったときの音が控えめになります。",
        "O volume dos cliques do menu. Baixo suaviza o som quando o ponteiro passa sobre algo.",
        "Il volume dei clic del menu. Bassi attenua il suono quando il puntatore passa sopra qualcosa."),
    # Return to RiftWii (Settings)
    "Wii Menu button": ("Botón Menú de Wii", "Wiiメニューボタン", "Botão Menu Wii", "Pulsante Menu Wii"),
    "Back to RiftWii": ("Volver a RiftWii", "RiftWiiに戻る", "Voltar ao RiftWii", "Torna a RiftWii"),
    "The Wii Menu button in a game's HOME Menu brings you back to RiftWii. It needs the RiftWii channel installed.": (
        "El botón Menú de Wii del menú HOME de un juego te devuelve a RiftWii. Necesita el canal de RiftWii instalado.",
        "ゲームのHOMEメニューの「Wiiメニュー」ボタンでRiftWiiに戻ります。RiftWiiチャンネルのインストールが必要です。",
        "O botão Menu Wii do menu HOME de um jogo leva você de volta ao RiftWii. Precisa do canal do RiftWii instalado.",
        "Il pulsante Menu Wii del menu HOME di un gioco ti riporta a RiftWii. Serve il canale RiftWii installato."),
    "The Wii Menu button in a game's HOME Menu can bring you back to RiftWii once the RiftWii channel is installed (Settings).": (
        "El botón Menú de Wii del menú HOME de un juego puede devolverte a RiftWii cuando instales el canal de RiftWii (Ajustes).",
        "RiftWiiチャンネルをインストールすると(設定)、ゲームのHOMEメニューの「Wiiメニュー」ボタンでRiftWiiに戻れます。",
        "O botão Menu Wii do menu HOME de um jogo pode levar você de volta ao RiftWii depois de instalar o canal do RiftWii (Configurações).",
        "Il pulsante Menu Wii del menu HOME di un gioco può riportarti a RiftWii una volta installato il canale RiftWii (Impostazioni)."),
    # Menu music (Settings)
    "Menu music": ("Música del menú", "メニューの音楽", "Música do menu", "Musica del menu"),
    "In-game screenshots": ("Capturas en el juego", "ゲーム中のスクリーンショット", "Capturas no jogo", "Screenshot nel gioco"),
    "Experimental. In a game, hold 1 and press HOME (GameCube controller: hold L and R, press Down). Pictures go to sd:/riftwii/screenshots when RiftWii next starts. Some games and mods don't work with it.": (
        "Experimental. En un juego, mantén 1 y pulsa HOME (mando de GameCube: mantén L y R y pulsa Abajo). Las imágenes van a sd:/riftwii/screenshots al iniciar RiftWii. Algunos juegos y mods no funcionan con esto.",
        "試験的な機能です。ゲーム中に1を押したままHOME(ゲームキューブコントローラーはLとRを押したまま下)。画像は次の起動時にsd:/riftwii/screenshotsに保存されます。使えないゲームやMODもあります。",
        "Experimental. Num jogo, segure 1 e aperte HOME (controle de GameCube: segure L e R e aperte Baixo). As imagens vão para sd:/riftwii/screenshots quando o RiftWii abrir de novo. Alguns jogos e mods não funcionam com isso.",
        "Sperimentale. In un gioco, tieni premuto 1 e premi HOME (controller GameCube: tieni L e R e premi Giù). Le immagini vanno in sd:/riftwii/screenshots al prossimo avvio di RiftWii. Alcuni giochi e mod non funzionano."),
    "Music while the menu is open: music.ogg from sd:/riftwii, or the one in RiftWii's own folder.": (
        "Música mientras el menú está abierto: music.ogg de sd:/riftwii, o la de la carpeta de RiftWii.",
        "メニューを開いている間の音楽です: sd:/riftwiiのmusic.ogg、なければRiftWiiのフォルダのものを流します。",
        "Música enquanto o menu está aberto: music.ogg de sd:/riftwii, ou a da pasta do RiftWii.",
        "Musica mentre il menu è aperto: music.ogg da sd:/riftwii, o quella nella cartella di RiftWii."),
    "No music.ogg found in sd:/riftwii or in RiftWii's own folder.": (
        "No hay ningún music.ogg en sd:/riftwii ni en la carpeta de RiftWii.",
        "sd:/riftwiiにもRiftWiiのフォルダにもmusic.oggがありません。",
        "Nenhum music.ogg em sd:/riftwii nem na pasta do RiftWii.",
        "Nessun music.ogg in sd:/riftwii né nella cartella di RiftWii."),
    "On a Wii U the GameCube adapter may not work in game from the front USB ports; the rear ones work.": (
        "En una Wii U, el adaptador de GameCube puede no funcionar en el juego desde los puertos USB delanteros; los traseros sí funcionan.",
        "Wii Uでは、前面のUSBポートだとゲーム中にゲームキューブアダプターが動かないことがあります。背面のポートなら動きます。",
        "No Wii U, o adaptador de GameCube pode não funcionar no jogo pelas portas USB da frente; as de trás funcionam.",
        "Su Wii U l'adattatore GameCube potrebbe non funzionare in gioco dalle porte USB anteriori; quelle posteriori funzionano."),
    # Packs on USB
    "Packs on USB are experimental; if it fails, copy them to SD.": (
        "Los packs en USB son experimentales; si falla, cópialos a la SD.",
        "USBのパックは試験的な機能です。うまくいかない場合はSDにコピーしてください。",
        "Packs no USB são experimentais; se falhar, copie-os para o SD.",
        "I pacchetti su USB sono sperimentali; se non funziona, copiali sulla SD."),
    # A pack that starts a Homebrew Channel app (CTGP Revolution): not working yet.
    "CTGP Revolution (a pack that starts a Homebrew Channel app) does not work from RiftWii yet: it stops on a black or green screen. Start CTGP from the Homebrew Channel instead.": (
        "CTGP Revolution (un pack que inicia una app del Homebrew Channel) todavía no funciona desde RiftWii: se queda en una pantalla negra o verde. Inicia CTGP desde el Homebrew Channel.",
        "CTGP Revolution（Homebrew Channelのアプリを起動するパック）はまだRiftWiiから動きません。黒または緑の画面で止まります。CTGPはHomebrew Channelから起動してください。",
        "O CTGP Revolution (um pack que inicia um app do Homebrew Channel) ainda não funciona pelo RiftWii: ele para numa tela preta ou verde. Inicie o CTGP pelo Homebrew Channel.",
        "CTGP Revolution (un pacchetto che avvia un'app dell'Homebrew Channel) non funziona ancora da RiftWii: si ferma su una schermata nera o verde. Avvia CTGP dall'Homebrew Channel."),
    "CTGP Revolution does not work from RiftWii yet (see Start).": (
        "CTGP Revolution todavía no funciona desde RiftWii (mira Jugar).",
        "CTGP RevolutionはまだRiftWiiから動きません（「はじめる」を参照）。",
        "O CTGP Revolution ainda não funciona pelo RiftWii (veja Jogar).",
        "CTGP Revolution non funziona ancora da RiftWii (vedi Gioca)."),
    # Mods where they cannot work (refused at Start). {1} is a folder like
    # "usb:/Project+", maybe followed by the piece below.
    " (and {1} more)": (" (y {1} más)", " (ほか{1}件)", " (e mais {1})", " (e altri {1})"),
    "Code builds on the USB drive won't work. Move {1} to the SD card.": (
        "Las builds de códigos en el USB no funcionan. Mueve {1} a la SD.",
        "USBドライブのコードビルドは動きません。{1}をSDカードに移してください。",
        "Builds de códigos no USB não funcionam. Mova {1} para o SD.",
        "Le build di codici sull'unità USB non funzionano. Sposta {1} sulla SD."),
    "This won't work: code builds like Project+ have to be on the SD card, not the USB drive. Move {1} to the same spot on your SD card and try again. Your games can stay on USB.": (
        "Así no va a funcionar: las builds de códigos como Project+ tienen que estar en la SD, no en el USB. Mueve {1} al mismo sitio de tu SD y vuelve a intentarlo. Los juegos pueden seguir en el USB.",
        "このままでは動きません。Project+などのコードビルドはUSBドライブではなくSDカードに置く必要があります。{1}をSDカードの同じ場所に移して、もう一度試してください。ゲームはUSBのままで大丈夫です。",
        "Assim não vai funcionar: builds de códigos como o Project+ precisam estar no SD, não no USB. Mova {1} para o mesmo lugar no seu SD e tente de novo. Os jogos podem ficar no USB.",
        "Così non funziona: le build di codici come Project+ devono stare sulla SD, non sull'unità USB. Sposta {1} nello stesso punto della SD e riprova. I giochi possono restare su USB."),
    "This SD card ({1} GB) is too big for code builds. Make an SD image.": (
        "Esta SD ({1} GB) es demasiado grande para las builds de códigos. Crea una imagen SD.",
        "このSDカード({1} GB)はコードビルドには大きすぎます。SDイメージを作ってください。",
        "Este SD ({1} GB) é grande demais para builds de códigos. Crie uma imagem SD.",
        "Questa SD ({1} GB) è troppo grande per le build di codici. Crea un'immagine SD."),
    "This won't work: this SD card is bigger than 32 GB ({1} GB, SDXC), and Brawl only reads SD cards up to 32 GB, so the build's files never load. Pick \"Make an SD image...\" under the build on the Mods page: the game then reads a small card inside a file on this one. Or copy the build to a 32 GB or smaller card.": (
        "Así no va a funcionar: esta SD tiene más de 32 GB ({1} GB, SDXC) y Brawl solo lee tarjetas SD de hasta 32 GB, así que los archivos de la build nunca se cargan. Elige \"Crear una imagen SD...\" bajo la build en la página de Mods: el juego leerá entonces una tarjeta pequeña dentro de un archivo de esta. O copia la build a una tarjeta de 32 GB o menos.",
        "このままでは動きません。このSDカードは32 GBより大きく({1} GB、SDXC)、ブロールは32 GBまでのSDカードしか読めないため、ビルドのファイルが読み込まれません。MODページでビルドの下の「SDイメージを作る...」を選ぶと、ゲームはこのカードの中のファイルにある小さなカードを読みます。または32 GB以下のカードにビルドをコピーしてください。",
        "Assim não vai funcionar: este SD tem mais de 32 GB ({1} GB, SDXC) e o Brawl só lê cartões SD de até 32 GB, então os arquivos da build nunca carregam. Escolha \"Criar uma imagem SD...\" sob a build na página de Mods: o jogo passa a ler um cartão pequeno dentro de um arquivo deste. Ou copie a build para um cartão de 32 GB ou menos.",
        "Così non funziona: questa SD supera i 32 GB ({1} GB, SDXC) e Brawl legge solo schede SD fino a 32 GB, quindi i file della build non vengono mai caricati. Scegli \"Crea un'immagine SD...\" sotto la build nella pagina Mod: il gioco leggerà una piccola scheda dentro un file di questa. Oppure copia la build su una scheda da 32 GB o meno."),
    "Code builds need the game on USB or disc, not the SD card.": (
        "Las builds de códigos necesitan el juego en USB o en disco, no en la SD.",
        "コードビルドはSDカードではなく、USBかディスクのゲームで遊んでください。",
        "Builds de códigos precisam do jogo no USB ou no disco, não no SD.",
        "Le build di codici vogliono il gioco su USB o su disco, non sulla SD."),
    "This won't work: code builds like Project+ read the SD card while you play, so the game can't be on the SD card too. Put it on a USB drive, use the disc, or pick \"Make an SD image...\" under the build on the Mods page.": (
        "Así no va a funcionar: las builds de códigos como Project+ leen la SD mientras juegas, así que el juego no puede estar también en la SD. Ponlo en un USB, usa el disco o elige \"Crear una imagen SD...\" bajo la build en la página de Mods.",
        "このままでは動きません。Project+などのコードビルドはプレイ中にSDカードを読むので、ゲームをSDカードに置くことはできません。USBドライブに入れるか、ディスクを使うか、MODページでビルドの下の「SDイメージを作る...」を選んでください。",
        "Assim não vai funcionar: builds de códigos como o Project+ leem o SD enquanto você joga, então o jogo não pode estar no SD também. Coloque-o num USB, use o disco ou escolha \"Criar uma imagem SD...\" sob a build na página de Mods.",
        "Così non funziona: le build di codici come Project+ leggono la SD mentre giochi, quindi il gioco non può stare anche sulla SD. Mettilo su un'unità USB, usa il disco o scegli \"Crea un'immagine SD...\" sotto la build nella pagina Mod."),
    "Make an SD image...": ("Crear una imagen SD...", "SDイメージを作る...", "Criar uma imagem SD...", "Crea un'immagine SD..."),
    "{1} is on the USB drive: the game has to be there too.": (
        "{1} está en el USB: el juego también tiene que estar ahí.",
        "{1}はUSBドライブにあります。ゲームもUSBドライブに置いてください。",
        "{1} está no USB: o jogo também precisa estar lá.",
        "{1} è sull'unità USB: anche il gioco deve stare lì."),
    "This won't work: {1} is on the USB drive, and RiftWii reads the USB drive during a game only when the game is on it too. Put the game on the USB drive, or put {1} in the riftwii folder on your SD card.": (
        "Así no va a funcionar: {1} está en el USB, y RiftWii solo lee el USB durante una partida cuando el juego también está en él. Pon el juego en el USB, o pon {1} en la carpeta riftwii de tu SD.",
        "このままでは動きません。{1}はUSBドライブにありますが、RiftWiiがゲーム中にUSBドライブを読むのは、ゲームもUSBドライブにあるときだけです。ゲームをUSBドライブに置くか、{1}をSDカードのriftwiiフォルダーに入れてください。",
        "Assim não vai funcionar: {1} está no USB, e a RiftWii só lê o USB durante o jogo quando o jogo também está nele. Coloque o jogo no USB, ou coloque {1} na pasta riftwii do seu SD.",
        "Così non funziona: {1} è sull'unità USB, e RiftWii legge l'unità USB durante una partita solo se anche il gioco è lì. Metti il gioco sull'unità USB, oppure metti {1} nella cartella riftwii della SD."),
    # Updates (the pop-ups at start, and the Settings row)
    "Updating RiftWii": ("Actualizando RiftWii", "RiftWiiを更新しています", "Atualizando a RiftWii", "Aggiornamento di RiftWii"),
    "RiftWii {1} is out. Downloading and installing it now; this takes a minute...": (
        "Ya salió RiftWii {1}. Se está descargando e instalando; tarda un minuto...",
        "RiftWii {1}が出ています。ダウンロードしてインストールしています。1分ほどかかります...",
        "Saiu a RiftWii {1}. Baixando e instalando agora; leva um minuto...",
        "È uscita RiftWii {1}. La sto scaricando e installando; ci vuole un minuto..."),
    "Update failed": ("La actualización falló", "更新に失敗しました", "A atualização falhou", "Aggiornamento non riuscito"),
    # {1} the new version, {2} why, {3} where to get it.
    "RiftWii {1} could not be installed: {2}. This version keeps working; the new one is at {3}": (
        "No se pudo instalar RiftWii {1}: {2}. Esta versión sigue funcionando; la nueva está en {3}",
        "RiftWii {1}をインストールできませんでした: {2}。このバージョンはそのまま使えます。新しいものは{3}にあります",
        "Não foi possível instalar a RiftWii {1}: {2}. Esta versão continua funcionando; a nova está em {3}",
        "Impossibile installare RiftWii {1}: {2}. Questa versione continua a funzionare; la nuova è su {3}"),
    "OK": ("Aceptar", "OK", "OK", "OK"),
    "RiftWii updated": ("RiftWii actualizada", "RiftWiiを更新しました", "RiftWii atualizada", "RiftWii aggiornata"),
    # {1} the new version, {2} where it was written.
    "RiftWii {1} is installed ({2}). It runs the next time RiftWii starts. Leave to the Homebrew Channel now and start it again?": (
        "RiftWii {1} está instalada ({2}). Se usará la próxima vez que se inicie RiftWii. ¿Volver ahora al Homebrew Channel para iniciarla otra vez?",
        "RiftWii {1}をインストールしました ({2})。次にRiftWiiを起動したときに使われます。今Homebrew Channelに戻って、もう一度起動しますか?",
        "A RiftWii {1} está instalada ({2}). Ela será usada na próxima vez que a RiftWii iniciar. Voltar agora ao Homebrew Channel e iniciá-la de novo?",
        "RiftWii {1} è installata ({2}). Verrà usata al prossimo avvio di RiftWii. Tornare ora all'Homebrew Channel e riavviarla?"),
    "Leave": ("Salir", "終了", "Sair", "Esci"),
    "Later": ("Más tarde", "あとで", "Depois", "Più tardi"),
    "RiftWii {1} is installed. Start RiftWii again to use it.": (
        "RiftWii {1} está instalada. Inicia RiftWii de nuevo para usarla.",
        "RiftWii {1}をインストールしました。使うにはRiftWiiをもう一度起動してください。",
        "A RiftWii {1} está instalada. Inicie a RiftWii de novo para usá-la.",
        "RiftWii {1} è installata. Avvia di nuovo RiftWii per usarla."),
    "Update available": ("Actualización disponible", "更新があります", "Atualização disponível", "Aggiornamento disponibile"),
    # {1} the new version, {2} this one.
    "RiftWii {1} is out (this is {2}). Update now? It takes a minute.": (
        "Ya salió RiftWii {1} (esta es {2}). ¿Actualizar ahora? Tarda un minuto.",
        "RiftWii {1}が出ています (これは{2}です)。今すぐ更新しますか? 1分ほどかかります。",
        "Saiu a RiftWii {1} (esta é a {2}). Atualizar agora? Leva um minuto.",
        "È uscita RiftWii {1} (questa è la {2}). Aggiornare ora? Ci vuole un minuto."),
    "Not now": ("Ahora no", "今はしない", "Agora não", "Non ora"),
    "Are you sure?": ("¿Seguro?", "よろしいですか?", "Tem certeza?", "Sei sicuro?"),
    "Are you sure you don't want to update? If you had an issue, it could have been fixed in the latest update!": (
        "¿Seguro que no quieres actualizar? ¡Si tenías algún problema, puede que la última versión ya lo solucione!",
        "本当に更新しませんか? 何か問題があった場合、最新の更新で直っているかもしれません!",
        "Tem certeza de que não quer atualizar? Se você teve algum problema, ele pode ter sido corrigido na última atualização!",
        "Sei sicuro di non voler aggiornare? Se hai avuto un problema, potrebbe essere stato risolto nell'ultimo aggiornamento!"),
    "RiftWii {1} is out. Settings > Check for a new version installs it.": (
        "Ya salió RiftWii {1}. Ajustes > Buscar una versión nueva la instala.",
        "RiftWii {1}が出ています。設定 > 新しいバージョンを確認 でインストールできます。",
        "Saiu a RiftWii {1}. Configurações > Procurar uma versão nova a instala.",
        "È uscita RiftWii {1}. Impostazioni > Cerca una nuova versione la installa."),
    "Updates": ("Actualizaciones", "アップデート", "Atualizações", "Aggiornamenti"),
    "Beta": ("Beta", "ベータ", "Beta", "Beta"),
    "Stable": ("Estable", "安定版", "Estável", "Stabile"),
    "Beta: every new version, including test builds that may have new bugs. For testers.": (
        "Beta: cada versión nueva, incluidas las de prueba, que pueden tener fallos nuevos. Para testers.",
        "ベータ: テスト版を含むすべての新しいバージョンです。新しい不具合があるかもしれません。テスター向けです。",
        "Beta: toda versão nova, incluindo as de teste, que podem ter bugs novos. Para testadores.",
        "Beta: ogni nuova versione, comprese quelle di prova, che possono avere nuovi bug. Per i tester."),
    "Stable: only versions marked stable, which testers have checked.": (
        "Estable: solo las versiones marcadas como estables, que los testers han probado.",
        "安定版: テスターが確認した、安定版とされたバージョンだけです。",
        "Estável: só as versões marcadas como estáveis, que os testadores verificaram.",
        "Stabile: solo le versioni segnate come stabili, già controllate dai tester."),
    "No stable version is out yet. This is RiftWii {1}.": (
        "Aún no hay ninguna versión estable. Esta es RiftWii {1}.",
        "安定版はまだ出ていません。これはRiftWii {1}です。",
        "Ainda não saiu nenhuma versão estável. Esta é a RiftWii {1}.",
        "Non è ancora uscita una versione stabile. Questa è RiftWii {1}."),
    # Drive and SD card problems (pop-ups at start)
    "SD card: {1}": ("Tarjeta SD: {1}", "SDカード: {1}", "Cartão SD: {1}", "Scheda SD: {1}"),
    "USB drive: {1}": ("Unidad USB: {1}", "USBドライブ: {1}", "Unidade USB: {1}", "Unità USB: {1}"),
    "Drive problem": ("Problema con una unidad", "ドライブの問題", "Problema em uma unidade", "Problema con un'unità"),
    "Games on that drive are not listed. Check the drive on a computer; details are in sd:/riftwii/session.log.": (
        "Los juegos de esa unidad no aparecen. Revisa la unidad en un ordenador; los detalles están en sd:/riftwii/session.log.",
        "そのドライブのゲームは表示されません。パソコンでドライブを確認してください。詳しくはsd:/riftwii/session.logにあります。",
        "Os jogos dessa unidade não aparecem. Verifique a unidade em um computador; os detalhes estão em sd:/riftwii/session.log.",
        "I giochi di quell'unità non sono elencati. Controlla l'unità su un computer; i dettagli sono in sd:/riftwii/session.log."),
    "SD card problems": ("Problemas con la tarjeta SD", "SDカードの問題", "Problemas no cartão SD", "Problemi con la scheda SD"),
    "The SD card had trouble while the last game was saving. The details are in sd:/riftwii/cardlog.txt; please send that file to the RiftWii developers.": (
        "La tarjeta SD tuvo problemas mientras el último juego guardaba. Los detalles están en sd:/riftwii/cardlog.txt; envía ese archivo a los desarrolladores de RiftWii.",
        "前回のゲームのセーブ中にSDカードで問題が起きました。詳しくはsd:/riftwii/cardlog.txtにあります。このファイルをRiftWiiの開発者に送ってください。",
        "O cartão SD teve problemas enquanto o último jogo salvava. Os detalhes estão em sd:/riftwii/cardlog.txt; envie esse arquivo aos desenvolvedores da RiftWii.",
        "La scheda SD ha avuto problemi mentre l'ultimo gioco salvava. I dettagli sono in sd:/riftwii/cardlog.txt; invia quel file agli sviluppatori di RiftWii."),
    # Starting a game
    "Before you play": ("Antes de jugar", "遊ぶ前に", "Antes de jogar", "Prima di giocare"),
    "Press Start again to play.": (
        "Pulsa Jugar otra vez para empezar.",
        "もう一度「はじめる」を押すと遊べます。",
        "Aperte Jogar de novo para começar.",
        "Premi di nuovo Gioca per iniziare."),
    "Game cIOS": ("cIOS del juego", "ゲームのcIOS", "cIOS do jogo", "cIOS del gioco"),
    # The RiftWii channel
    "Add RiftWii to the Wii Menu?": (
        "¿Añadir RiftWii al Menú Wii?", "RiftWiiをWiiメニューに追加しますか?", "Adicionar a RiftWii ao Menu Wii?",
        "Aggiungere RiftWii al Menu Wii?"),
    "RiftWii can have a channel on the Wii Menu, so it starts without the Homebrew Channel. The channel only starts RiftWii from your SD card: RiftWii's updates keep working and the channel never needs reinstalling. The channel installer opens, then brings you back here. Settings can open it again later.": (
        "RiftWii puede tener un canal en el Menú Wii, para iniciarse sin el Homebrew Channel. El canal solo inicia RiftWii desde tu tarjeta SD: las actualizaciones de RiftWii siguen funcionando y el canal nunca hay que reinstalarlo. Se abre el instalador del canal y luego vuelves aquí. Ajustes puede abrirlo de nuevo más tarde.",
        "RiftWiiはWiiメニューにチャンネルを置けるので、Homebrew Channelなしで起動できます。チャンネルはSDカードのRiftWiiを起動するだけなので、RiftWiiの更新はそのまま使え、チャンネルを入れ直す必要はありません。チャンネルのインストーラーが開き、そのあとここに戻ります。あとで設定からもう一度開けます。",
        "A RiftWii pode ter um canal no Menu Wii, para iniciar sem o Homebrew Channel. O canal só inicia a RiftWii do seu cartão SD: as atualizações da RiftWii continuam funcionando e o canal nunca precisa ser reinstalado. O instalador do canal abre e depois traz você de volta aqui. As Configurações podem abri-lo de novo mais tarde.",
        "RiftWii può avere un canale nel Menu Wii, così si avvia senza l'Homebrew Channel. Il canale avvia solo RiftWii dalla tua scheda SD: gli aggiornamenti di RiftWii continuano a funzionare e il canale non va mai reinstallato. Si apre l'installer del canale, che poi ti riporta qui. Le Impostazioni possono riaprirlo più tardi."),
    "Open installer": ("Abrir instalador", "インストーラーを開く", "Abrir instalador", "Apri l'installer"),
    "No thanks": ("No, gracias", "いいえ", "Não, obrigado", "No, grazie"),
    "RiftWii channel on the Wii Menu": (
        "Canal de RiftWii en el Menú Wii", "WiiメニューのRiftWiiチャンネル", "Canal da RiftWii no Menu Wii",
        "Canale RiftWii nel Menu Wii"),
    "Installed": ("Instalado", "インストール済み", "Instalado", "Installato"),
    "Add": ("Añadir", "追加", "Adicionar", "Aggiungi"),
    "Open the channel installer?": (
        "¿Abrir el instalador del canal?", "チャンネルのインストーラーを開きますか?", "Abrir o instalador do canal?",
        "Aprire l'installer del canale?"),
    "RiftWii closes and the channel installer opens. It adds, updates or removes the RiftWii channel, then brings you back here.": (
        "RiftWii se cierra y se abre el instalador del canal. Añade, actualiza o quita el canal de RiftWii y luego te trae de vuelta aquí.",
        "RiftWiiを閉じてチャンネルのインストーラーを開きます。RiftWiiチャンネルの追加、更新、削除をして、ここに戻ってきます。",
        "A RiftWii fecha e o instalador do canal abre. Ele adiciona, atualiza ou remove o canal da RiftWii e depois traz você de volta aqui.",
        "RiftWii si chiude e si apre l'installer del canale. Aggiunge, aggiorna o rimuove il canale RiftWii, poi ti riporta qui."),
    "Open": ("Abrir", "開く", "Abrir", "Apri"),
    "Cancel": ("Cancelar", "キャンセル", "Cancelar", "Annulla"),
    # The screen shown while the installer starts: the line above the title.
    "The RiftWii channel": ("El canal de RiftWii", "RiftWiiチャンネル", "O canal da RiftWii", "Il canale RiftWii"),
    "Opening the installer for": ("Abriendo el instalador de", "インストーラーを開いています", "Abrindo o instalador de",
                                  "Apertura dell'installer per"),
    "RiftWii starts again when it is done.": (
        "RiftWii se inicia de nuevo al terminar.", "終わるとRiftWiiがもう一度起動します。",
        "A RiftWii inicia de novo quando terminar.", "RiftWii si riavvia quando ha finito."),
    "A Wii Menu channel that starts RiftWii from the SD card. It holds no copy of RiftWii, so updates keep working. Opens the channel installer, to add, update or remove it.": (
        "Un canal del Menú Wii que inicia RiftWii desde la tarjeta SD. No lleva ninguna copia de RiftWii, así que las actualizaciones siguen funcionando. Abre el instalador del canal para añadirlo, actualizarlo o quitarlo.",
        "SDカードのRiftWiiを起動するWiiメニューのチャンネルです。RiftWiiのコピーは入っていないので、更新はそのまま使えます。チャンネルのインストーラーを開いて、追加、更新、削除ができます。",
        "Um canal do Menu Wii que inicia a RiftWii pelo cartão SD. Ele não tem nenhuma cópia da RiftWii, então as atualizações continuam funcionando. Abre o instalador do canal, para adicioná-lo, atualizá-lo ou removê-lo.",
        "Un canale del Menu Wii che avvia RiftWii dalla scheda SD. Non contiene una copia di RiftWii, quindi gli aggiornamenti continuano a funzionare. Apre l'installer del canale, per aggiungerlo, aggiornarlo o rimuoverlo."),
    # Why the channel installer can not be opened.
    "Copy apps/riftwii_channel from the RiftWii zip to the SD card first": (
        "Copia primero apps/riftwii_channel del zip de RiftWii a la tarjeta SD",
        "先にRiftWiiのzipにあるapps/riftwii_channelをSDカードにコピーしてください",
        "Copie primeiro apps/riftwii_channel do zip da RiftWii para o cartão SD",
        "Copia prima apps/riftwii_channel dallo zip di RiftWii sulla scheda SD"),
    "Dolphin checks real signatures, so the channel can only be installed on a Wii": (
        "Dolphin comprueba las firmas reales, así que el canal solo se puede instalar en una Wii",
        "Dolphinは本物の署名を確認するので、チャンネルはWiiにしかインストールできません",
        "O Dolphin verifica as assinaturas reais, então o canal só pode ser instalado em um Wii",
        "Dolphin controlla le firme reali, quindi il canale si può installare solo su una Wii"),
    "Installing the channel needs a d2x cIOS in slot 249, 250 or 251": (
        "Para instalar el canal hace falta un cIOS d2x en el slot 249, 250 o 251",
        "チャンネルのインストールには、スロット249、250、251のどれかにd2x cIOSが必要です",
        "Instalar o canal precisa de um cIOS d2x no slot 249, 250 ou 251",
        "Per installare il canale serve un cIOS d2x nello slot 249, 250 o 251"),
    "(Log: sd:/riftwii/session.log)": (
        "(Registro: sd:/riftwii/session.log)", "(ログ: sd:/riftwii/session.log)",
        "(Log: sd:/riftwii/session.log)", "(Log: sd:/riftwii/session.log)"),
    # No SD card at start (USB mode is not supported yet)
    "RiftWii needs an SD card": (
        "RiftWii necesita una tarjeta SD", "RiftWiiにはSDカードが必要です", "A RiftWii precisa de um cartão SD",
        "RiftWii ha bisogno di una scheda SD"),
    "USB mode is not supported yet": (
        "El modo USB aún no es compatible", "USBモードにはまだ対応していません", "O modo USB ainda não é suportado",
        "La modalità USB non è ancora supportata"),
    "No SD card was found": (
        "No se encontró ninguna tarjeta SD", "SDカードが見つかりませんでした", "Nenhum cartão SD foi encontrado",
        "Nessuna scheda SD trovata"),
    "RiftWii was started from a USB drive. It keeps its settings, logs and saves on the SD card, so for now it needs one to run. Copy the sd-card folder from the RiftWii zip to a FAT32 SD card, put the card in the Wii and start RiftWii from it.": (
        "RiftWii se inició desde una unidad USB. Guarda sus ajustes, registros y partidas en la tarjeta SD, así que por ahora necesita una para funcionar. Copia la carpeta sd-card del zip de RiftWii a una tarjeta SD en FAT32, ponla en la Wii e inicia RiftWii desde ella.",
        "RiftWiiはUSBドライブから起動されました。設定、ログ、セーブはSDカードに保存するので、今のところ動かすにはSDカードが必要です。RiftWiiのzipにあるsd-cardフォルダをFAT32のSDカードにコピーし、Wiiに入れて、そこからRiftWiiを起動してください。",
        "A RiftWii foi iniciada de uma unidade USB. Ela guarda as configurações, os registros e os saves no cartão SD, então por enquanto precisa de um para funcionar. Copie a pasta sd-card do zip da RiftWii para um cartão SD em FAT32, coloque o cartão no Wii e inicie a RiftWii por ele.",
        "RiftWii è stata avviata da un'unità USB. Tiene impostazioni, log e salvataggi sulla scheda SD, quindi per ora ne serve una per funzionare. Copia la cartella sd-card dallo zip di RiftWii su una scheda SD in FAT32, inseriscila nella Wii e avvia RiftWii da lì."),
    "RiftWii keeps its settings, logs and saves on the SD card and could not read one. Put a FAT32 SD card in the Wii with the sd-card folder from the RiftWii zip on it, then start RiftWii again.": (
        "RiftWii guarda sus ajustes, registros y partidas en la tarjeta SD y no pudo leer ninguna. Pon en la Wii una tarjeta SD en FAT32 con la carpeta sd-card del zip de RiftWii y vuelve a iniciar RiftWii.",
        "RiftWiiは設定、ログ、セーブをSDカードに保存しますが、SDカードを読み込めませんでした。RiftWiiのzipにあるsd-cardフォルダを入れたFAT32のSDカードをWiiに入れて、もう一度RiftWiiを起動してください。",
        "A RiftWii guarda as configurações, os registros e os saves no cartão SD e não conseguiu ler nenhum. Coloque no Wii um cartão SD em FAT32 com a pasta sd-card do zip da RiftWii e inicie a RiftWii de novo.",
        "RiftWii tiene impostazioni, log e salvataggi sulla scheda SD e non è riuscita a leggerne una. Inserisci nella Wii una scheda SD in FAT32 con la cartella sd-card dello zip di RiftWii, poi riavvia RiftWii."),
    "Need a card? Scan this.": (
        "¿Necesitas una? Escanea esto.", "カードが必要ならこれをスキャン", "Precisa de um? Escaneie isto.",
        "Ti serve una scheda? Scansiona qui."),
    # Burned discs (a d2x cIOS reads them on older Wiis); {1} a cIOS slot.
    "Is this a burned disc?": (
        "¿Es un disco grabado?", "焼いたディスクですか?", "É um disco gravado?", "È un disco masterizzato?"),
    "The drive could not read this disc. If it is a burned disc, RiftWii can read it through d2x on older Wiis (later Wii drives read only Nintendo discs). Burned discs can wear out the disc drive sooner: use them at your own risk. The menu then restarts under IOS{1} for this session.": (
        "La unidad no pudo leer este disco. Si es un disco grabado, RiftWii puede leerlo con d2x en las Wii más antiguas (las unidades posteriores solo leen discos de Nintendo). Los discos grabados pueden desgastar antes la unidad: úsalos bajo tu propia responsabilidad. El menú se reinicia entonces con el IOS{1} para esta sesión.",
        "ドライブがこのディスクを読めませんでした。焼いたディスクなら、古いWiiではRiftWiiがd2x経由で読めます (後期のWiiのドライブは任天堂のディスクしか読めません)。焼いたディスクはディスクドライブの寿命を縮めることがあります。自己責任で使ってください。その場合、このセッションの間メニューをIOS{1}で再起動します。",
        "A unidade não conseguiu ler este disco. Se for um disco gravado, a RiftWii pode lê-lo pelo d2x nos Wii mais antigos (as unidades posteriores só leem discos da Nintendo). Discos gravados podem desgastar a unidade mais cedo: use-os por sua conta e risco. O menu então reinicia no IOS{1} nesta sessão.",
        "L'unità non è riuscita a leggere questo disco. Se è un disco masterizzato, RiftWii può leggerlo tramite d2x sulle Wii più vecchie (le unità successive leggono solo dischi Nintendo). I dischi masterizzati possono consumare prima l'unità: usali a tuo rischio. Il menu si riavvia quindi con l'IOS{1} per questa sessione."),
    "Burned disc: it wears the Wii's disc drive more than a pressed disc does. Play at your own risk.": (
        "Disco grabado: desgasta la unidad de la Wii más que un disco original. Juega bajo tu propia responsabilidad.",
        "焼いたディスクです。正規のディスクよりWiiのドライブに負担がかかります。自己責任で遊んでください。",
        "Disco gravado: ele desgasta a unidade do Wii mais que um disco original. Jogue por sua conta e risco.",
        "Disco masterizzato: consuma l'unità della Wii più di un disco originale. Gioca a tuo rischio."),
    "Try with d2x": ("Probar con d2x", "d2xで試す", "Tentar com d2x", "Prova con d2x"),
    "The menu runs under IOS{1} for this session, to read burned discs. Pick the disc.": (
        "El menú usa el IOS{1} en esta sesión para leer discos grabados. Elige el disco.",
        "焼いたディスクを読むため、このセッションではメニューがIOS{1}で動いています。ディスクを選んでください。",
        "O menu usa o IOS{1} nesta sessão para ler discos gravados. Escolha o disco.",
        "In questa sessione il menu usa l'IOS{1} per leggere i dischi masterizzati. Scegli il disco."),
    "The drive cannot read this disc, even through d2x. Later Wii drives read only Nintendo discs, never burned ones; on an older Wii, the burn may be bad.": (
        "La unidad no puede leer este disco, ni siquiera con d2x. Las unidades posteriores de Wii solo leen discos de Nintendo, nunca grabados; en una Wii antigua, puede que la grabación esté mal.",
        "d2xを使ってもドライブがこのディスクを読めません。後期のWiiのドライブは任天堂のディスクしか読めず、焼いたディスクは読めません。古いWiiなら、書き込みが失敗しているかもしれません。",
        "A unidade não consegue ler este disco, nem pelo d2x. As unidades posteriores do Wii só leem discos da Nintendo, nunca gravados; num Wii antigo, a gravação pode estar ruim.",
        "L'unità non riesce a leggere questo disco, nemmeno tramite d2x. Le unità Wii successive leggono solo dischi Nintendo, mai masterizzati; su una Wii più vecchia, la masterizzazione potrebbe essere difettosa."),
    "The drive cannot read this disc. If it is a burned disc, RiftWii needs a d2x cIOS to read it.": (
        "La unidad no puede leer este disco. Si es un disco grabado, RiftWii necesita un cIOS d2x para leerlo.",
        "ドライブがこのディスクを読めません。焼いたディスクを読むには、RiftWiiにd2x cIOSが必要です。",
        "A unidade não consegue ler este disco. Se for um disco gravado, a RiftWii precisa de um cIOS d2x para lê-lo.",
        "L'unità non riesce a leggere questo disco. Se è un disco masterizzato, RiftWii ha bisogno di un cIOS d2x per leggerlo."),
    "RiftWii could not restart. Start it again from the Homebrew Channel.": (
        "RiftWii no pudo reiniciarse. Vuelve a iniciarla desde el Homebrew Channel.",
        "RiftWiiを再起動できませんでした。Homebrew Channelからもう一度起動してください。",
        "A RiftWii não conseguiu reiniciar. Inicie-a de novo pelo Homebrew Channel.",
        "RiftWii non è riuscita a riavviarsi. Avviala di nuovo dall'Homebrew Channel."),
    "1 game": (
        "1 juego",
        "ゲーム1本",
        "1 jogo",
        "1 gioco"),
    "{1} games": (
        "{1} juegos",
        "ゲーム{1}本",
        "{1} jogos",
        "{1} giochi"),
    # Search ({1}: the words typed).
    "Search \"{1}\"": ("Búsqueda \"{1}\"", "検索「{1}」", "Busca \"{1}\"", "Ricerca \"{1}\""),
    "No game matches \"{1}\". Press 1 for all games.": (
        "Ningún juego coincide con \"{1}\". Pulsa 1 para ver todos.",
        "「{1}」に一致するゲームはありません。1ですべてのゲームを表示します。",
        "Nenhum jogo corresponde a \"{1}\". Aperte 1 para ver todos.",
        "Nessun gioco corrisponde a \"{1}\". Premi 1 per vederli tutti."),
    "1: all games": ("1: todos los juegos", "1: すべてのゲーム", "1: todos os jogos", "1: tutti i giochi"),
    "Search games": ("Buscar juegos", "ゲームを検索", "Buscar jogos", "Cerca giochi"),
    "Search": ("Buscar", "検索", "Buscar", "Cerca"),
    "Space": ("Espacio", "スペース", "Espaço", "Spazio"),
    "Clear": ("Borrar", "全消去", "Limpar", "Cancella"),
    # The key that deletes the last letter: keep it short (a 48-wide key).
    "Del": ("Bor", "削除", "Apg", "Canc"),
    "Homebrew Channel": (
        "Homebrew Channel",
        "Homebrew Channel",
        "Homebrew Channel",
        "Homebrew Channel"),
    "Wii Menu": (
        "Menú de Wii",
        "Wiiメニュー",
        "Menu Wii",
        "Menu Wii"),
    "Power off": (
        "Apagar",
        "電源を切る",
        "Desligar",
        "Spegni"),
    "HOME Menu": (
        "Menú HOME",
        "HOMEメニュー",
        "Menu HOME",
        "Menu HOME"),
    "Close": (
        "Cerrar",
        "とじる",
        "Fechar",
        "Chiudi"),
    "Opens the HOME Menu, as HOME does: the Homebrew Channel, the Wii Menu, Priiloader or power off.": (
        "Abre el menú HOME, como el botón HOME: Homebrew Channel, menú de Wii, Priiloader o apagar.",
        "HOMEボタンと同じく、HOMEメニューを開きます: Homebrew Channel、Wiiメニュー、Priiloader、電源を切る。",
        "Abre o menu HOME, como o botão HOME: Homebrew Channel, Menu Wii, Priiloader ou desligar.",
        "Apre il menu HOME, come il tasto HOME: Homebrew Channel, Menu Wii, Priiloader o spegnimento."),
    # Problem reports
    "Sending a report": ("Enviando un informe", "レポートを送信中", "Enviando um relatório", "Invio della segnalazione"),
    "Gathering the logs and sending them. This can take half a minute...": (
        "Reuniendo los registros y enviándolos. Puede tardar medio minuto...",
        "ログを集めて送信しています。30秒ほどかかることがあります...",
        "Juntando os registros e enviando. Pode levar meio minuto...",
        "Raccolta dei log e invio. Può richiedere mezzo minuto..."),
    "Report not sent": ("Informe no enviado", "レポートを送信できませんでした", "Relatório não enviado", "Segnalazione non inviata"),
    "It could not be sent: {1}. It is saved on the SD card as sd:/riftwii/report.txt: send that file instead.": (
        "No se pudo enviar: {1}. Está guardado en la tarjeta SD como sd:/riftwii/report.txt: envía ese archivo.",
        "送信できませんでした: {1}。SDカードに sd:/riftwii/report.txt として保存したので、代わりにそのファイルを送ってください。",
        "Não foi possível enviar: {1}. Ele está salvo no cartão SD como sd:/riftwii/report.txt: envie esse arquivo.",
        "Impossibile inviarla: {1}. È salvata sulla scheda SD come sd:/riftwii/report.txt: invia quel file."),
    "It could not be sent or saved: {1}": (
        "No se pudo enviar ni guardar: {1}",
        "送信も保存もできませんでした: {1}",
        "Não foi possível enviar nem salvar: {1}",
        "Impossibile inviarla o salvarla: {1}"),
    "Send this link to whoever is helping you, or scan the code with a phone:": (
        "Envía este enlace a quien te esté ayudando, o escanea el código con un móvil:",
        "手伝ってくれている人にこのリンクを送るか、スマートフォンでコードを読み取ってください:",
        "Envie este link para quem está ajudando você, ou leia o código com um celular:",
        "Invia questo link a chi ti sta aiutando, oppure inquadra il codice con un telefono:"),
    "The report was too big, so only its start was kept.": (
        "El informe era demasiado grande y solo se guardó el principio.",
        "レポートが大きすぎたため、最初の部分だけが保存されました。",
        "O relatório era grande demais, então só o começo foi mantido.",
        "La segnalazione era troppo grande, quindi ne è stato tenuto solo l'inizio."),
    "Report sent": ("Informe enviado", "レポートを送信しました", "Relatório enviado", "Segnalazione inviata"),
    "It holds RiftWii's logs and settings, the game's choices and packs, and which console, IOS and controllers this is. It goes to paste.rs, or dpaste.com when paste.rs can't be reached, where anyone with its link can read it.": (
        "Incluye los registros y ajustes de RiftWii, las opciones y packs del juego, y qué consola, IOS y mandos son. Se envía a paste.rs, o a dpaste.com si no se puede llegar a paste.rs, donde cualquiera con el enlace puede leerlo.",
        "RiftWiiのログと設定、ゲームの選択とパック、本体・IOS・コントローラーの情報が入っています。paste.rsに送られ（paste.rsにつながらないときはdpaste.com）、リンクを知っている人なら誰でも読めます。",
        "Ele traz os registros e as configurações do RiftWii, as escolhas e os packs do jogo, e qual console, IOS e controles são. Vai para o paste.rs, ou para o dpaste.com se o paste.rs não puder ser alcançado, onde qualquer pessoa com o link pode lê-lo.",
        "Contiene i log e le impostazioni di RiftWii, le scelte e i pack del gioco, e quali console, IOS e controller sono. Va su paste.rs, o su dpaste.com se paste.rs non è raggiungibile, dove chiunque abbia il link può leggerla."),
    "RiftWii crashed last time": ("RiftWii se bloqueó la última vez", "前回RiftWiiがクラッシュしました", "O RiftWii travou da última vez", "L'ultima volta RiftWii si è bloccato"),
    "The game crashed last time": ("El juego se bloqueó la última vez", "前回ゲームがクラッシュしました", "O jogo travou da última vez", "L'ultima volta il gioco si è bloccato"),
    "The last launch failed": ("El último inicio falló", "前回の起動に失敗しました", "A última inicialização falhou", "L'ultimo avvio non è riuscito"),
    "Send a report of what happened?": (
        "¿Enviar un informe de lo que pasó?",
        "何が起きたかのレポートを送信しますか？",
        "Enviar um relatório do que aconteceu?",
        "Inviare una segnalazione di quello che è successo?"),
    "Send": ("Enviar", "送信", "Enviar", "Invia"),
    "Send a problem report": ("Enviar un informe de problema", "問題のレポートを送信", "Enviar um relatório de problema", "Invia una segnalazione di problema"),
    "Send a problem report?": ("¿Enviar un informe de problema?", "問題のレポートを送信しますか？", "Enviar um relatório de problema?", "Inviare una segnalazione di problema?"),
    "Something went wrong? Sends what it takes to find out to a paste site, and shows a link to pass on.": (
        "¿Algo salió mal? Envía a un sitio de pegado lo necesario para averiguarlo y muestra un enlace para compartir.",
        "問題が起きましたか？原因を調べるのに必要な情報を共有サイトに送り、共有用のリンクを表示します。",
        "Algo deu errado? Envia a um site de textos o necessário para descobrir e mostra um link para compartilhar.",
        "Qualcosa non va? Invia a un sito di paste quello che serve per capirlo e mostra un link da condividere."),
    # GameCube adapter On, on a Wii U
    "WARNING: this can freeze your Wii U": (
        "AVISO: esto puede congelar tu Wii U",
        "警告：Wii Uがフリーズすることがあります",
        "AVISO: isto pode travar o seu Wii U",
        "ATTENZIONE: può bloccare la tua Wii U"),
    "On a Wii U, the GameCube adapter in games can freeze the console at 97% while a game from the SD card or a USB drive starts. It works some times and freezes others, and RiftWii cannot tell beforehand. If it freezes, hold the power button to turn the console off. Automatic is safe: it leaves the adapter out of those games.": (
        "En una Wii U, el adaptador de GameCube en los juegos puede congelar la consola al 97% mientras arranca un juego de la tarjeta SD o de una unidad USB. A veces funciona y otras se congela, y RiftWii no puede saberlo antes. Si se congela, mantén pulsado el botón de encendido para apagar la consola. Automático es seguro: deja el adaptador fuera de esos juegos.",
        "Wii Uでは、SDカードやUSBドライブのゲームを起動するとき、ゲーム中のゲームキューブ用アダプターが原因で97%で本体がフリーズすることがあります。動くときもフリーズするときもあり、RiftWiiには事前にわかりません。フリーズしたら電源ボタンを長押しして本体の電源を切ってください。「自動」なら安全です：それらのゲームではアダプターを使いません。",
        "Num Wii U, o adaptador de GameCube nos jogos pode travar o console em 97% enquanto um jogo do cartão SD ou de uma unidade USB inicia. Às vezes funciona e às vezes trava, e o RiftWii não consegue saber antes. Se travar, segure o botão de energia para desligar o console. Automático é seguro: deixa o adaptador fora desses jogos.",
        "Su una Wii U, l'adattatore GameCube nei giochi può bloccare la console al 97% mentre parte un gioco dalla scheda SD o da un'unità USB. A volte funziona e altre si blocca, e RiftWii non può saperlo prima. Se si blocca, tieni premuto il tasto di accensione per spegnere la console. Automatico è sicuro: lascia l'adattatore fuori da quei giochi."),
    "Turn it on anyway": (
        "Activarlo de todos modos",
        "それでもオンにする",
        "Ativar mesmo assim",
        "Attivalo comunque"),
    "Some game launches WILL freeze and need the power button. Only turn this on to test the adapter, and send a problem report when it freezes. You can set it back to Automatic here at any time.": (
        "Algunos arranques de juegos SE congelarán y necesitarán el botón de encendido. Actívalo solo para probar el adaptador, y envía un informe de problema cuando se congele. Puedes volver a Automático aquí cuando quieras.",
        "一部のゲームの起動は必ずフリーズし、電源ボタンが必要になります。アダプターを試すときだけオンにして、フリーズしたら問題のレポートを送ってください。ここでいつでも「自動」に戻せます。",
        "Algumas inicializações de jogos VÃO travar e precisar do botão de energia. Só ative para testar o adaptador, e envie um relatório de problema quando travar. Você pode voltar para Automático aqui quando quiser.",
        "Alcuni avvii dei giochi SI bloccheranno e richiederanno il tasto di accensione. Attivalo solo per provare l'adattatore e invia una segnalazione di problema quando si blocca. Puoi tornare ad Automatico qui in qualsiasi momento."),
    "Yes, turn it on": (
        "Sí, activarlo",
        "はい、オンにする",
        "Sim, ativar",
        "Sì, attivalo"),
    "Keep it as it is": (
        "Dejarlo como está",
        "このままにする",
        "Deixar como está",
        "Lascialo com'è"),
    "WARNING: on this Wii U, games from the SD card or a USB drive can freeze at 97% with this on. Use Automatic unless you are testing the adapter.": (
        "AVISO: en esta Wii U, los juegos de la tarjeta SD o de una unidad USB pueden congelarse al 97% con esto activado. Usa Automático salvo que estés probando el adaptador.",
        "警告：このWii Uでは、これがオンだとSDカードやUSBドライブのゲームが97%でフリーズすることがあります。アダプターを試すとき以外は「自動」にしてください。",
        "AVISO: neste Wii U, jogos do cartão SD ou de uma unidade USB podem travar em 97% com isto ativado. Use Automático a menos que esteja testando o adaptador.",
        "ATTENZIONE: su questa Wii U, i giochi dalla scheda SD o da un'unità USB possono bloccarsi al 97% con questo attivo. Usa Automatico a meno che tu non stia provando l'adattatore."),
    "Left as it was.": (
        "Se dejó como estaba.",
        "変更しませんでした。",
        "Deixado como estava.",
        "Lasciato com'era."),
    # Themes (wii/menutheme.cpp, docs/THEMES.md)
    "Theme": ("Tema", "テーマ", "Tema", "Tema"),
    "Default": ("Predeterminado", "標準", "Padrão", "Predefinito"),
    "The menu's colours and pictures. Themes are folders in sd:/riftwii/themes (docs/THEMES.md on GitHub).": (
        "Los colores y las imágenes del menú. Los temas son carpetas en sd:/riftwii/themes (docs/THEMES.md en GitHub).",
        "メニューの色と画像です。テーマはsd:/riftwii/themesのフォルダーです（GitHubのdocs/THEMES.md）。",
        "As cores e as imagens do menu. Os temas são pastas em sd:/riftwii/themes (docs/THEMES.md no GitHub).",
        "I colori e le immagini del menu. I temi sono cartelle in sd:/riftwii/themes (docs/THEMES.md su GitHub)."),
    "No themes in sd:/riftwii/themes. The zip's themes folder has one to copy there.": (
        "No hay temas en sd:/riftwii/themes. La carpeta themes del zip tiene uno para copiar allí.",
        "sd:/riftwii/themesにテーマがありません。zipのthemesフォルダーにコピーできるテーマがあります。",
        "Não há temas em sd:/riftwii/themes. A pasta themes do zip tem um para copiar para lá.",
        "Nessun tema in sd:/riftwii/themes. La cartella themes dello zip ne ha uno da copiare lì."),
    "Restart the menu?": ("¿Reiniciar el menú?", "メニューを再起動しますか？", "Reiniciar o menu?", "Riavviare il menu?"),
    "RiftWii's menu restarts to show {1}. Your games and settings stay as they are.": (
        "El menú de RiftWii se reinicia para mostrar {1}. Tus juegos y ajustes se quedan como están.",
        "{1}を表示するためにRiftWiiのメニューを再起動します。ゲームと設定はそのままです。",
        "O menu do RiftWii reinicia para mostrar {1}. Seus jogos e configurações continuam como estão.",
        "Il menu di RiftWii si riavvia per mostrare {1}. I tuoi giochi e le impostazioni restano come sono."),
    "Restart": ("Reiniciar", "再起動", "Reiniciar", "Riavvia"),
    "Theme: {1}": ("Tema: {1}", "テーマ: {1}", "Tema: {1}", "Tema: {1}"),
    "Clock": ("Reloj", "時計", "Relógio", "Orologio"),
    "12-hour": ("12 horas", "12時間", "12 horas", "12 ore"),
    "24-hour": ("24 horas", "24時間", "24 horas", "24 ore"),
    "How Home's clock writes the time: 12-hour (with AM and PM) or 24-hour. Automatic writes it as the menu's language does.": ("Cómo escribe la hora el reloj de Inicio: 12 horas (con a. m. y p. m.) o 24 horas. Automático la escribe como el idioma del menú.", "ホームの時計の表示: 12時間（午前・午後つき）か24時間。自動ではメニューの言語に合わせます。", "Como o relógio do Início mostra a hora: 12 horas (com AM e PM) ou 24 horas. Automático mostra como o idioma do menu.", "Come l'orologio della Home scrive l'ora: 12 ore (con AM e PM) o 24 ore. Automatico la scrive come la lingua del menu."),
    "Menu font": ("Fuente del menú", "メニューのフォント", "Fonte do menu", "Carattere del menu"),
    # The Mods page's picture popup, above the file name, for a mod without a picture.
    "Add a picture:": ("Añade una imagen:", "画像を追加:", "Adicione uma imagem:", "Aggiungi un'immagine:"),
    "The letters the menu is written in: RiftWii's own, or the Wii Menu's, read from this Wii.": (
        "Las letras con las que se escribe el menú: las de RiftWii, o las del Menú de Wii, leídas de esta Wii.",
        "メニューの文字のフォントです。RiftWii独自のものか、このWiiから読み込むWiiメニューのものを使います。",
        "As letras em que o menu é escrito: as do RiftWii, ou as do Menu do Wii, lidas deste Wii.",
        "Le lettere con cui è scritto il menu: quelle di RiftWii, o quelle del Menu Wii, lette da questa Wii."),
    "the Wii Menu's font": ("la fuente del Menú de Wii", "Wiiメニューのフォント", "a fonte do Menu do Wii",
                            "il carattere del Menu Wii"),
    "RiftWii's font": ("la fuente de RiftWii", "RiftWiiのフォント", "a fonte do RiftWii", "il carattere di RiftWii"),
    "Menu font: {1}": ("Fuente del menú: {1}", "メニューのフォント: {1}", "Fonte do menu: {1}", "Carattere del menu: {1}"),
    "RiftWii stopped while reading the Wii Menu's font last time, so it uses its own. Send a problem report so this can be fixed.": (
        "RiftWii se detuvo al leer la fuente del Menú de Wii la última vez, así que usa la suya. Envía un informe de problemas para que se pueda arreglar.",
        "前回、Wiiメニューのフォントを読み込んでいる間にRiftWiiが止まったため、RiftWiiのフォントを使います。直せるように、問題レポートを送ってください。",
        "O RiftWii parou ao ler a fonte do Menu Wii da última vez, então usa a própria. Envie um relatório de problema para que isso seja corrigido.",
        "L'ultima volta RiftWii si è fermato leggendo il font del Menu Wii, quindi usa il suo. Invia una segnalazione così si può correggere."),
    "The Wii Menu's font could not be read, so RiftWii's is used.": (
        "No se pudo leer la fuente del Menú de Wii, así que se usa la de RiftWii.",
        "Wiiメニューのフォントを読み込めなかったため、RiftWiiのフォントを使います。",
        "Não foi possível ler a fonte do Menu do Wii, então a do RiftWii é usada.",
        "Non è stato possibile leggere il carattere del Menu Wii, quindi si usa quello di RiftWii."),
    "Files from a Mac on the SD card": ("Archivos de un Mac en la tarjeta SD", "SDカードにMacのファイルがあります",
        "Arquivos de um Mac no cartão SD", "File di un Mac sulla scheda SD"),
    'This SD card has hidden files that macOS makes when it copies (names that start with "._"). RiftWii skips them, but they fill the card and can confuse other homebrew. To remove them, put the card in your Mac, open Terminal and type: dot_clean -m /Volumes/ followed by the card\'s name. On Windows, delete the files whose names start with "._".': (
        "Esta tarjeta SD tiene archivos ocultos que macOS crea al copiar (nombres que empiezan por \"._\"). "
        "RiftWii los ignora, pero ocupan espacio y pueden confundir a otros homebrew. Para quitarlos, pon la tarjeta "
        "en tu Mac, abre Terminal y escribe: dot_clean -m /Volumes/ seguido del nombre de la tarjeta. En Windows, "
        "borra los archivos cuyo nombre empieza por \"._\".",
        "このSDカードには、macOSがコピー時に作る隠しファイル（名前が「._」で始まるもの）があります。"
        "RiftWiiは無視しますが、容量を使い、他のHomebrewを混乱させることがあります。削除するには、カードをMacに入れ、"
        "ターミナルで dot_clean -m /Volumes/ に続けてカードの名前を入力してください。Windowsでは、名前が「._」で始まるファイルを削除してください。",
        "Este cartão SD tem arquivos ocultos que o macOS cria ao copiar (nomes que começam com \"._\"). "
        "O RiftWii os ignora, mas eles ocupam espaço e podem confundir outros homebrews. Para removê-los, coloque o cartão "
        "no seu Mac, abra o Terminal e digite: dot_clean -m /Volumes/ seguido do nome do cartão. No Windows, "
        "apague os arquivos cujos nomes começam com \"._\".",
        "Questa scheda SD ha file nascosti che macOS crea quando copia (nomi che iniziano con \"._\"). "
        "RiftWii li ignora, ma occupano spazio e possono confondere altri homebrew. Per rimuoverli, inserisci la scheda "
        "nel tuo Mac, apri il Terminale e scrivi: dot_clean -m /Volumes/ seguito dal nome della scheda. Su Windows, "
        "elimina i file il cui nome inizia con \"._\"."),
    "This code mod can't start": ("Este mod de códigos no puede iniciarse", "このコードModは起動できません",
        "Este mod de códigos não pode iniciar", "Questo mod di codici non può partire"),
    "{1} has more codes than the game has room for ({2}, room for {3}). It needs the file gameconfig.txt (or gc.txt) from the same download as the mod. Copy it into the mod's own folder on the SD card, {4}, so it can't replace another mod's. Not in the download? Ask whoever made the mod.": (
        '{1} tiene más códigos de los que caben en el juego ({2}, hay sitio para {3}). Necesita el archivo gameconfig.txt (o gc.txt) de la misma descarga que el mod. Cópialo en la carpeta del propio mod en la tarjeta SD, {4}, así no reemplaza el de otro mod. ¿No está en la descarga? Pregunta a quien hizo el mod.',
        '{1}のコードはゲームに入る量を超えています（{2}、空きは{3}）。Modと同じダウンロードに入っているgameconfig.txt（またはgc.txt）が必要です。ほかのModのものを上書きしないよう、SDカードのそのMod自身のフォルダ（{4}）にコピーしてください。ダウンロードにない場合は、Modの作者に聞いてください。',
        '{1} tem mais códigos do que cabem no jogo ({2}, espaço para {3}). Ele precisa do arquivo gameconfig.txt (ou gc.txt) do mesmo download do mod. Copie-o para a pasta do próprio mod no cartão SD, {4}, assim ele não substitui o de outro mod. Não está no download? Pergunte a quem fez o mod.',
        '{1} ha più codici di quanti ne entrino nel gioco ({2}, spazio per {3}). Serve il file gameconfig.txt (o gc.txt) dallo stesso download del mod. Copialo nella cartella del mod stesso sulla scheda SD, {4}, così non sostituisce quello di un altro mod. Non è nel download? Chiedi a chi ha fatto il mod.'),
    "The codes take {1}, but {2} only makes room for {3}. Turn off some cheats on this game's Cheats page, or turn off a code mod.": (
        "Los códigos ocupan {1}, pero {2} solo deja sitio para {3}. Desactiva algunos trucos en la página de Trucos de "
        "este juego, o desactiva un mod de códigos.",
        "コードは{1}ありますが、{2}が用意する場所は{3}だけです。このゲームのチートのページでチートをいくつかオフにするか、"
        "コードModをオフにしてください。",
        "Os códigos ocupam {1}, mas {2} só reserva espaço para {3}. Desligue algumas trapaças na página de Trapaças deste "
        "jogo, ou desligue um mod de códigos.",
        "I codici occupano {1}, ma {2} lascia spazio solo per {3}. Disattiva alcuni trucchi nella pagina Trucchi di "
        "questo gioco, o disattiva un mod di codici."),
    "Aspect ratio": ("Relación de aspecto", "アスペクト比", "Proporção da tela", "Proporzioni"),
    "Rumble": ("Vibración", "振動", "Vibração", "Vibrazione"),
    "Wii Remote speaker": ("Altavoz del mando de Wii", "Wiiリモコンのスピーカー", "Alto-falante do Wii Remote",
        "Altoparlante del telecomando Wii"),
    "Region strings fix": ("Corrección de textos de región", "地域文字列の修正", "Correção de textos de região",
        "Correzione testi della regione"),
    "Makes the game use 4:3 or widescreen 16:9 whatever the Wii's TV setting says. Not every game can be changed.": (
        "Hace que el juego use 4:3 o panorámico 16:9 diga lo que diga el ajuste de TV de la Wii. No todos los juegos se pueden cambiar.",
        "Wiiのテレビ設定に関係なく、ゲームを4:3かワイドの16:9にします。変更できないゲームもあります。",
        "Faz o jogo usar 4:3 ou widescreen 16:9, seja qual for o ajuste de TV do Wii. Nem todo jogo pode ser mudado.",
        "Fa usare al gioco 4:3 o il panoramico 16:9, qualunque sia l'impostazione TV della Wii. Non tutti i giochi si possono cambiare."),
    "Off: the Wii Remotes never rumble in this game.": (
        "Desactivado: los mandos de Wii nunca vibran en este juego.", "オフ: このゲームではWiiリモコンが振動しません。",
        "Desligado: os Wii Remotes nunca vibram neste jogo.", "Disattivato: i telecomandi Wii non vibrano mai in questo gioco."),
    "Off: no sound from the Wii Remotes' speakers in this game.": (
        "Desactivado: sin sonido de los altavoces de los mandos de Wii en este juego.",
        "オフ: このゲームではWiiリモコンのスピーカーから音が出ません。",
        "Desligado: sem som dos alto-falantes dos Wii Remotes neste jogo.",
        "Disattivato: nessun suono dagli altoparlanti dei telecomandi Wii in questo gioco."),
    "For a game from another region (an import): the game sees its own region's country names where it looks for the console's.": (
        "Para un juego de otra región (importado): el juego ve los nombres de país de su región donde busca los de la consola.",
        "ほかの地域のゲーム（輸入版）用: 本体の国名を探す場所で、ゲームは自分の地域の国名を見ます。",
        "Para um jogo de outra região (importado): o jogo vê os nomes de país da sua região onde procura os do console.",
        "Per un gioco di un'altra regione (d'importazione): il gioco vede i nomi dei paesi della sua regione dove cerca quelli della console."),
    "Don't show again": ("No volver a mostrar", "今後表示しない", "Não mostrar de novo", "Non mostrare più"),
    # Make an SD image and the code build picker (3.3)
    "Report sent: {1}": ("Informe enviado: {1}", "レポートを送信しました: {1}", "Relatório enviado: {1}", "Segnalazione inviata: {1}"),
    "SD image": ("Imagen SD", "SDイメージ", "Imagem SD", "Immagine SD"),
    "Looking at the build's files...": ("Revisando los archivos de la build...", "ビルドのファイルを調べています...", "Verificando os arquivos da build...", "Controllo dei file della build..."),
    "No SD image made": ("No se creó ninguna imagen SD", "SDイメージは作られませんでした", "Nenhuma imagem SD criada", "Nessuna immagine SD creata"),
    " and ": (" y ", "と", " e ", " e "),
    "RiftWii copies {1} into sd:/riftwii/{2} ({3}). The game then gets the image as its SD card, so it can be on the SD card too. This takes about {4} minutes.": ("RiftWii copia {1} en sd:/riftwii/{2} ({3}). El juego usa la imagen como su tarjeta SD, así que también puede estar en la tarjeta SD. Esto tarda unos {4} minutos.", "RiftWiiは{1}をsd:/riftwii/{2} ({3})にコピーします。ゲームはこのイメージをSDカードとして使うので、ゲームもSDカードに置けます。{4}分ほどかかります。", "O RiftWii copia {1} em sd:/riftwii/{2} ({3}). O jogo então usa a imagem como seu cartão SD, assim ele também pode ficar no cartão SD. Isso leva cerca de {4} minutos.", "RiftWii copia {1} in sd:/riftwii/{2} ({3}). Il gioco usa l'immagine come sua scheda SD, così può stare anche sulla scheda SD. Ci vogliono circa {4} minuti."),
    "It is saved in {1} parts, as a FAT32 card holds no file of 4 GB.": ("Se guarda en {1} partes, ya que una tarjeta en FAT32 no admite archivos de 4 GB.", "FAT32のカードは4 GBのファイルを保存できないため、{1}個に分けて保存されます。", "É salva em {1} partes, já que um cartão em FAT32 não suporta arquivos de 4 GB.", "È salvata in {1} parti, dato che una scheda in FAT32 non supporta file da 4 GB."),
    "It replaces the {1} there now.": ("Reemplaza el {1} que hay ahora.", "今ある{1}を置き換えます。", "Substitui o {1} que está lá agora.", "Sostituisce il {1} che c'è ora."),
    "Make an SD image?": ("¿Crear una imagen SD?", "SDイメージを作りますか?", "Criar uma imagem SD?", "Creare un'immagine SD?"),
    "Make it": ("Crearla", "作成", "Criar", "Creala"),
    "Making {1}": ("Creando {1}", "{1}を作成中", "Criando {1}", "Creazione di {1}"),
    "Stop": ("Detener", "中止", "Parar", "Interrompi"),
    "{1} minutes left": ("Faltan {1} minutos", "残り{1}分", "Faltam {1} minutos", "Mancano {1} minuti"),
    "{1} seconds left": ("Faltan {1} segundos", "残り{1}秒", "Faltam {1} segundos", "Mancano {1} secondi"),
    "Free space": ("Espacio libre", "空き容量", "Espaço livre", "Spazio libero"),
    "Stopped. Nothing was kept.": ("Detenido. No se guardó nada.", "中止しました。何も保存されませんでした。", "Interrompido. Nada foi guardado.", "Interrotto. Non è stato salvato nulla."),
    "{1} could not be made: {2}. Nothing was kept.": ("No se pudo crear {1}: {2}. No se guardó nada.", "{1}を作成できませんでした: {2}。何も保存されませんでした。", "Não foi possível criar {1}: {2}. Nada foi guardado.", "Impossibile creare {1}: {2}. Non è stato salvato nulla."),
    "SD image made": ("Imagen SD creada", "SDイメージを作成しました", "Imagem SD criada", "Immagine SD creata"),
    "{1} is in sd:/riftwii, and the build in it is turned on: the game gets the image as its SD card.": ("{1} está en sd:/riftwii y la build que contiene está activada: el juego usa la imagen como su tarjeta SD.", "{1}はsd:/riftwiiにあり、中のビルドはオンになっています: ゲームはこのイメージをSDカードとして使います。", "{1} está em sd:/riftwii e a build dentro dela está ativada: o jogo usa a imagem como seu cartão SD.", "{1} è in sd:/riftwii e la build al suo interno è attivata: il gioco usa l'immagine come sua scheda SD."),
    "Remove from the list": ("Quitar de la lista", "一覧から削除", "Remover da lista", "Rimuovi dall'elenco"),
    "Add a code build...": ("Añadir una build de códigos...", "コードビルドを追加...", "Adicionar uma build de códigos...", "Aggiungi una build di codici..."),
    "..  (up a folder)": ("..  (subir una carpeta)", "..  (上のフォルダへ)", "..  (subir uma pasta)", "..  (sali di una cartella)"),
    "Pick": ("Elegir", "選択", "Escolher", "Scegli"),
    "Nothing here": ("No hay nada aquí", "ここには何もありません", "Nada aqui", "Non c'è niente qui"),
    "Add a code build": ("Añadir una build de códigos", "コードビルドを追加", "Adicionar uma build de códigos", "Aggiungi una build di codici"),
    "Achievement unlocked: License Enthusiast": ("Logro desbloqueado: Entusiasta de las licencias", "実績解除: ライセンスマニア", "Conquista desbloqueada: Entusiasta de licenças", "Obiettivo sbloccato: Appassionato di licenze"),
    "Congratulations! You read all 5,644 words of the GNU GPL, version 3. Are you really that bored? Respect to the developers whose work RiftWii builds on: every one of you is credited above. The GPL is there to protect people who share their code, not to be waved around only when it's handy for picking a fight. Your reward: absolutely nothing, as the license says (\"WITHOUT ANY WARRANTY\").": ("¡Enhorabuena! Has leído las 5644 palabras de la GNU GPL, versión 3. ¿De verdad estás tan aburrido? Respeto a los desarrolladores en cuyo trabajo se basa RiftWii: cada uno de vosotros aparece en los créditos de arriba. La GPL está para proteger a quienes comparten su código, no para blandirla solo cuando conviene para buscar pelea. Tu recompensa: absolutamente nada, como dice la licencia (\"WITHOUT ANY WARRANTY\").", "おめでとうございます! GNU GPL バージョン3の全5,644語を読みましたね。そんなに暇だったんですか? RiftWiiが基盤としている開発者の皆さんに敬意を: 上に全員の名前がクレジットされています。GPLはコードを共有する人々を守るためのものであり、喧嘩を売るのに都合がいいときだけ振りかざすものではありません。ご褒美: ライセンスにある通り、まったく何もありません (\"WITHOUT ANY WARRANTY\")。", "Parabéns! Você leu todas as 5.644 palavras da GNU GPL, versão 3. Está tão entediado assim? Nosso respeito aos desenvolvedores em cujo trabalho o RiftWii se baseia: cada um de vocês está creditado acima. A GPL existe para proteger quem compartilha seu código, não para ser brandida só quando convém para arrumar briga. Sua recompensa: absolutamente nada, como diz a licença (\"WITHOUT ANY WARRANTY\").", "Congratulazioni! Hai letto tutte le 5.644 parole della GNU GPL, versione 3. Ti annoi davvero così tanto? Rispetto agli sviluppatori sul cui lavoro si basa RiftWii: ognuno di voi è accreditato sopra. La GPL serve a proteggere chi condivide il proprio codice, non a essere sbandierata solo quando fa comodo per attaccare briga. La tua ricompensa: assolutamente nulla, come dice la licenza (\"WITHOUT ANY WARRANTY\")."),
    "Fair enough": ("Me parece justo", "ごもっとも", "Justo", "Ci sta"),
    "Only a build in a folder can go into an image.": ("Solo una build en una carpeta puede ir en una imagen.", "イメージに入れられるのは、フォルダ内のビルドだけです。", "Só uma build em uma pasta pode ir para uma imagem.", "Solo una build in una cartella può andare in un'immagine."),
    "The build cannot go into an image: {1}.": ("La build no puede ir en una imagen: {1}.", "ビルドをイメージに入れられません: {1}。", "A build não pode ir para uma imagem: {1}.", "La build non può andare in un'immagine: {1}."),
    "The image needs {1} on the SD card and {2} is free. Make room (or use a bigger card) and try again.": ("La imagen necesita {1} en la tarjeta SD y hay {2} libres. Haz espacio (o usa una tarjeta más grande) y vuelve a intentarlo.", "イメージにはSDカードに{1}が必要ですが、空きは{2}です。空きを作るか、大きいカードを使って、もう一度試してください。", "A imagem precisa de {1} no cartão SD e há {2} livres. Libere espaço (ou use um cartão maior) e tente de novo.", "L'immagine richiede {1} sulla scheda SD e ci sono {2} liberi. Fai spazio (o usa una scheda più grande) e riprova."),
    "Cannot replace {1}.": ("No se puede reemplazar {1}.", "{1}を置き換えられません。", "Não é possível substituir {1}.", "Impossibile sostituire {1}."),
    "{1} has folders too deep to copy.": ("{1} tiene carpetas demasiado profundas para copiarlas.", "{1}には深すぎてコピーできないフォルダがあります。", "{1} tem pastas profundas demais para copiar.", "{1} ha cartelle troppo profonde da copiare."),
    "Cannot read sd:{1}.": ("No se puede leer sd:{1}.", "sd:{1} を読み込めません。", "Não é possível ler sd:{1}.", "Impossibile leggere sd:{1}."),
    "The build has more than {1} files.": ("La build tiene más de {1} archivos.", "ビルドのファイル数が{1}個を超えています。", "A build tem mais de {1} arquivos.", "La build ha più di {1} file."),
}


# Korean, a table of its own (msgid: text), from DDinghoya's translation
# (pull request 20). A msgid missing here shows in English.
KO = {
    "Games with mods":
        "MOD 지원 게임",
    "All games":
        "모든 게임",
    "Recently played":
        "최근 플레이",
    "No game on these drives was played from RiftWii yet. Press 1 for all games.":
        "이 드라이브에 있는 게임 중 RiftWii를 통해 실행된 게임은 아직 없습니다. 모든 게임을 보려면 1번을 누르세요.",
    "{1}/{2}":
        "{1}/{2}",
    "Played once, on {1}":
        "{1}에 1회 플레이",
    "Played {1} times, last on {2}":
        "{1} 회 플레이, {2} 마지막 플레이",
    "1: view   2: settings   -/+: pages   B: A to Z":
        "1: 보기   2: 설정   -/+: 페이지   B: A〜Z",
    "Page {1} of {2}":
        "{1} / {2} 페이지",
    "{1}: no games in /wbfs or /games":
        "{1}: /wbfs 또는 /games 폴더에 게임이 없음",
    "SD: no card":
        "SD: 카드 없음",
    "No d2x cIOS in 249-251: games cannot boot yet":
        "슬롯 249~251에 d2x cIOS가 설치되어 있지 않음: 아직 게임을 실행할 수 없음",
    "No game here has packs in sd:/riivolution yet. Press 1 for all games.":
        "아직 sd:/riivolution에 팩이 포함된 게임이 없습니다. 모든 게임을 보려면 1번을 누르세요.",
    "No games found (usb:/wbfs, usb:/games, sd:/wbfs, sd:/games)":
        "게임을 찾을 수 없음 (usb:/wbfs, usb:/games, sd:/wbfs, sd:/games)",
    "Reading the SD card...":
        "SD 카드 읽는 중...",
    "Reading the USB drive... (a big drive takes a moment)":
        "USB 드라이브를 읽는 중... (용량이 큰 드라이브는 시간이 다소 걸릴 수 있음)",
    "Reading the disc...":
        "디스크 읽는 중...",
    "scan failed":
        "스캔 실패",
    "(the menu runs under IOS {1}; USB drives need a base-58 cIOS for that, or set the menu IOS back to 58)":
        "(메뉴는 iOS {1}에서 실행됩니다. USB 드라이브를 사용하려면 base-58 cIOS가 필요하거나, 메뉴 iOS를 58로 다시 설정해야 합니다.)",
    "(after the failed launch RiftWii came back under IOS {1}, which cannot read the drive here; start RiftWii again from the Homebrew Channel)":
        "(출시 실패 후 RiftWii가 드라이브를 인식할 수 없는 IOS {1} 상태로 돌아갔습니다. 홈브류 채널l에서 RiftWii를 다시 실행하세요.)",
    "No disc in the drive":
        "드라이브에 디스크 없음",
    "Disc drive":
        "디스크 드라이브",
    "USB drive":
        "USB 드라이브",
    "SD card":
        "SD 카드",
    "Sun":
        "일",
    "Mon":
        "월",
    "Tue":
        "화",
    "Wed":
        "수",
    "Thu":
        "목",
    "Fri":
        "금",
    "Sat":
        "토",
    "{1}:{2} AM":
        "오전 {1}:{2}",
    "{1}:{2} PM":
        "오후 {1}:{2}",
    "{1} {2}/{3}":
        "{2}/{3} ({1})",
    "MODS":
        "MOD",
    "Back":
        "뒤로",
    "Start":
        "시작",
    "Saves":
        "저장",
    "On the Wii":
        "Wii 본체",
    "SD, from Wii save":
        "SD, Wii 세이브에서",
    "SD, fresh start":
        "SD, 처음부터",
    "Kept by the pack":
        "팩이 관리",
    "This pack keeps its own saves. Turn it off to choose here.":
        "이 팩은 자체 세이브 데이터를 사용합니다. 여기서 선택하려면 이 기능을 끄세요.",
    "Saves go to the SD card, starting from the Wii's save.":
        "세이브 데이터는 Wii의 세이브 데이터를 시작으로 SD 카드로 저장됩니다.",
    "Saves go to the SD card, starting fresh.":
        "저장 데이터는 SD 카드에 저장되며, 처음부터 새로 시작하게 됩니다.",
    "Saves stay on the Wii, as usual.":
        "세이브는 평소와 마찬가지로 Wii 본체에 저장됩니다.",
    "Broken":
        "오류",
    "On":
        "켬",
    "Off":
        "끔",
    "Mods":
        "MOD",
    "None":
        "없음",
    "{1} switched on":
        "{1} 개 켬",
    "On: {1}":
        "켬: {1}",
    "No mods for this game.":
        "이 게임에는 MOD가 없습니다.",
    "No mods on the SD card. Put Riivolution XML in sd:/riivolution.":
        "SD 카드에 MOD는 없습니다. Riivolution XML 파일을 sd:/riivolution 경로에 넣으세요.",
    "1 mod pack for this game. Press A to turn it on.":
        "이 게임용 MOD 팩이 1개 있습니다. A를 눌러 활성화하세요.",
    "{1} mod packs for this game. Press A to turn them on.":
        "{1} 이 게임용 MOD 팩입니다. A를 눌러 활성화하세요.",
    "No mods on the SD card":
        "SD 카드에 MOD 없음",
    "No mods for this game":
        "이 게임에는 모드가 없음",
    "Put Riivolution XML in sd:/riivolution":
        "Riivolution XML 파일을 sd:/riivolution에 넣기",
    "1 XML file is for another game":
        "1 XML 파일은 다른 게임용",
    "{1} XML files are for other games":
        "{1} XML 파일은 다른 게임용",
    "Package scan failed; go back and try again":
        "패키지 검색에 실패했습니다. 이전 단계로 돌아가 다시 시도하기",
    "This XML cannot be read; the error is listed under it.":
        "이 XML을 읽을 수 없습니다. 오류는 그 아래에 나열되어 있습니다.",
    "This XML cannot be read; fix it on the card and come back.":
        "이 XML을 읽을 수 없습니다. 카드에서 수정 후 다시 시도해 주세요.",
    "This pack cannot be turned on.":
        "이 팩은 켤 수 없습니다.",
    "Off. A turns it on.":
        "끔. A를 누르면 켜집니다.",
    "Off. A turns it on and shows its setting.":
        "끔. A가 전원을 켜고 설정을 보여줍니다.",
    "Off. A turns it on and shows its {1} settings.":
        "끔. A가 전원을 켜고 {1} 설정을 표시합니다.",
    "On. It applies as a whole.":
        "켬, 전체적으로 적용됩니다.",
    "On, but nothing chosen yet: pick its settings below.":
        "켬, 하지만 아직 설정이 선택되지 않았습니다. 아래에서 설정을 선택하세요.",
    "On, {1} of {2} settings chosen.":
        "켬, {2}개 설정 중 {1}개 선택됩니다.",
    "No game is selected; go back and pick one.":
        "선택된 게임이 없습니다; 돌아가서 게임을 선택하세요.",
    "Preparing the mods...":
        "MOD 준비 중...",
    "Cheats":
        "치트",
    "Picture width":
        "사진 너비",
    "Deflicker":
        "디플리커",
    "Black borders":
        "검은 테두리",
    "Region video fix":
        "지역 비디오 수정",
    "For a US or Japanese game that shows no picture on a console from another region: the game is told the video hardware matches its region.":
        "다른 지역의 콘솔에서 화면이 표시되지 않는 미국 또는 일본 게임의 경우, 해당 게임은 비디오 하드웨어가 자신의 지역과 일치한다고 인식하게 됩니다.",
    "Framebuffer":
        "프레임버퍼",
    "704 pixels":
        "704 픽셀",
    "720 pixels (full)":
        "720 픽셀 (전체)",
    "Game's own":
        "게임 자체",
    "Off (sharp)":
        "끔 (선명함)",
    "Low":
        "낮음",
    "Medium":
        "중간",
    "High":
        "높음",
    "Remove":
        "제거",
    "Keep":
        "유지하기",
    "Remove all (experimental)":
        "모두 제거 (실험적)",
    "Remove all":
        "모두 제거",
    "Default ({1})":
        "기본 ({1})",
    "On, none picked":
        "켬, 선택된 항목 없음",
    "On, {1} picked":
        "켬, {1} 개 선택됨",
    "Cheat codes for this game. Press A to choose them.":
        "이 게임의 치트 코드입니다. A를 눌러 선택하세요.",
    "How wide the picture is drawn. 720 fills the screen from side to side.":
        "화면이 얼마나 넓게 그려지는지를 나타냅니다. 720은 화면의 양쪽 끝을 가득 채웁니다.",
    "A filter that softens the picture to hide flicker. Off gives the sharpest picture.":
        "화면 깜빡임을 감추기 위해 이미지를 부드럽게 만드는 필터입니다. 끄면 가장 선명한 화면이 나옵니다.",
    "Remove stretches the picture over the bars at the sides. Remove all also stretches it over the bars at the top and bottom: experimental, some games show a broken picture or crash with it.":
        "'제거'는 사진을 좌우 여백(바)까지 확장합니다. '모두 제거'은 상하 여백까지 포함해 사진을 확장하는데, 이는 실험적인 기능이므로 일부 게임에서는 화면이 깨지거나 충돌이 발생할 수 있습니다.",
    "Remove takes away the bars at the sides; Remove all also the top and bottom (experimental).":
        "'제거'는 측면의 막대를 제거; '모두 제거'는 상단과 하단의 막대까지 모두 제거합니다. (실험적 기능)",
    "Last time, this game left black borders on all sides.":
        "지난번에 이 게임을 했을 때는 사방에 검은 테두리가 생겼습니다.",
    "Last time, this game left black borders at the sides.":
        "지난번에 이 게임을 했을 때는 양옆에 검은 테두리가 생겼습니다.",
    "Last time, this game left black borders at the top and bottom.":
        "지난번에 이 게임은 위아래에 검은색 테두리가 생겼습니다.",
    "Last time, this game filled the whole screen.":
        "지난번에는 이 게임이 화면 전체를 가득 채웠습니다.",
    "Use cheats":
        "치트 사용",
    "Get the latest cheats":
        "최신 치트 정보 확인",
    "Download cheats":
        "치트 다운로드",
    "Download":
        "다운로드",
    "Edit first":
        "먼저 편집",
    "The cheats are in {1}. Edit it on a computer to add your own.":
        "치트가 {1}에 있습니다. 컴퓨터에서 편집하여 직접 추가할 수 있습니다.",
    "Downloading cheats...":
        "치트 다운로드...",
    "{1} cheats. Turn on the ones you want.":
        "{1} 치트입니다. 원하는 항목을 켜세요.",
    "Could not download cheats: {1}":
        "치트를 다운로드할 수 없음: {1}",
    "No cheats found online for this game.":
        "이 게임에 대한 치트 정보는 온라인에서 찾을 수 없습니다.",
    "Cheats are only applied when this is On.":
        "이 기능이 켜져 있을 때만 치트가 적용됩니다.",
    "Downloads are off in Settings.":
        "설정에서 다운로드 기능이 꺼져 있습니다.",
    "No game is selected.":
        "선택된 게임이 없습니다.",
    "No cheat file yet. Choose Download to get one.":
        "아직 치트 파일이 없습니다. 다운로드를 선택하여 파일을 받으세요.",
    "No cheat file at {1}":
        "{1}에 치트 파일이 없",
    "The cheat file has no cheats in it: {1}":
        "치트 파일에 치트 없음: {1}",
    "Settings":
        "설정",
    "Language":
        "언어",
    "Wii: {1}":
        "Wii: {1}",
    "Download names and cheats":
        "이름 및 치트 다운로드",
    "Get the latest game names":
        "최신 게임 이름 확인",
    "Update":
        "업데이트",
    "Menu IOS":
        "메뉴 IOS",
    "Menu IOS: IOS 58 (no d2x cIOS found)":
        "메뉴 IOS: IOS 58 (d2x cIOS를 찾을 수 없음)",
    "Find network packs (RiiFS)":
        "네트워크 팩 (RiiFS) 찾기",
    "Copy network packs again":
        "네트워크 팩을 다시 복사",
    "Resync":
        "재동기화",
    "Look for games again":
        "다시 게임을 찾아보기",
    "Rescan":
        "재검색",
    "Leave RiftWii":
        "RiftWii 나가기",
    "Exit":
        "나가기",
    "These apply to every game. A game's own page can change them for that game.":
        "이 내용은 모든 게임에 적용됩니다. 개별 게임 페이지에서 해당 게임에 맞춰 이를 변경할 수 있습니다.",
    "Game names follow the language when they are downloaded.":
        "게임 이름은 다운로드 시 해당 언어를 따릅니다.",
    "Game names and cheats are downloaded when the Wii is online.":
        "Wii가 온라인 상태일 때 게임 이름과 치트가 다운로드됩니다.",
    "Nothing is downloaded. Names and cheats already on the card are still used.":
        "아무것도 다운로드되지 않습니다. 카드에 이미 들어 있는 이름과 치트가 그대로 사용됩니다.",
    "Downloads are off. Turn on Download names and cheats first.":
        "다운로드 기능이 꺼져 있습니다. 먼저 이름 및 치트 다운로드 기능을 켜세요.",
    "Downloading game names...":
        "게임 이름 다운로드 중...",
    "Game names updated.":
        "게임 이름이 업데이트되었습니다.",
    "Could not download game names: {1}":
        "게임 이름을 다운로드할 수 없음: {1}",
    "Cannot write sd:/riftwii/settings.txt":
        "sd:/riftwii/settings.txt 파일을 기록할 수 없음",
    "Cannot write sd:/riftwii/menu_ios.txt":
        "sd:/riftwii/menu_ios.txt 파일을 기록할 수 없음",
    "The menu runs under the Homebrew Channel's IOS (the default).":
        "메뉴는 홈브류 채널의 IOS (기본값)에서 실행됩니다.",
    "The menu and every game run under cIOS {1}, so a cIOS with fakemote makes USB DS3/DS4 pads work as Wii Remotes. USB drives in the menu need a base-58 cIOS.":
        "메뉴와 모든 게임은 cIOS {1} 환경에서 구동되므로, 'fakemote' 기능이 포함된 cIOS를 사용하면 USB로 연결된 DS3/DS4 패드를 Wii 리모컨처럼 사용할 수 있습니다. 메뉴에서 USB 드라이브를 사용하려면 base-58 cIOS가 필요합니다.",
    "Takes effect the next time RiftWii starts.":
        "다음에 RiftWii가 시작될 때 적용됩니다.",
    "Looks for a PC running a RiiFS server when the games are read. Rescan to look now.":
        "게임을 불러올 때 RiiFS 서버가 실행 중인 PC를 찾습니다. 지금 바로 다시 검색하려면 재검색을 수행하세요.",
    "Only servers named by <network> in an XML on the card are used.":
        "카드 내 XML의 <network>에 지정된 서버만 사용됩니다.",
    "The next launch copies every file of its network packs again.":
        "다음 실행 시 네트워크 팩의 모든 파일이 다시 복사됩니다.",
    "Starting":
        "시작중",
    "Dumping files from":
        "파일 덤프하기",
    "The game takes over the screen when it is ready.":
        "게임이 준비되면 화면을 가득 채웁니다.",
    "Opening the game...":
        "게임을 시작하는 중...",
    "GameCube adapter":
        "게임큐브 어댑터",
    "Check the GameCube adapter":
        "게임큐브 어댑터 확인",
    "Test":
        "테스트",
    "Welcome to RiftWii":
        "RiftWii에 오신 것을 환영합니다",
    "RiftWii starts your Wii games with Riivolution-format mods, from the disc, a USB drive or the SD card. Your game files are never changed. This short tour shows the basics.":
        "RiftWii는 디스크, USB 드라이브 또는 SD 카드에 있는 Wii 게임을 Riivolution 형식의 MOD와 함께 실행해 줍니다. 이때 원본 게임 파일은 전혀 변경되지 않습니다. 이 짧은 소개를 통해 기본적인 사용법을 확인해 보세요.",
    "Your games":
        "게임",
    "Put games in the wbfs or games folder at the top of the SD card or the USB drive (WBFS, ISO or RVZ). A disc in the drive shows up too. Home lists games that have mods first: press 1, or the round button at the bottom left, to see all your games.":
        "SD 카드나 USB 드라이브의 최상위 경로에 있는 `wbfs` 또는 `games` 폴더에 게임 파일(WBFS, ISO 또는 RVZ 형식)을 넣으세요. 드라이브에 삽입된 디스크도 함께 표시됩니다. 홈 화면에는 모드가 적용된 게임이 우선적으로 표시되지만, '1'번 버튼 (또는 왼쪽 하단의 원형 버튼)을 누르면 전체 게임 목록을 확인할 수 있습니다.",
    "Put mod packs (the XML file and the folders that come with it) in sd:/riivolution or usb:/riivolution. Pick a game, open Mods, switch a pack on and choose its options. Start (or +) plays the game with them.":
        "MOD 팩(XML 파일 및 관련 폴더)을 sd:/riivolution 또는 usb:/riivolution 경로에 넣으세요. 게임을 선택하고 'Mods'로 들어가 모드 팩을 활성화한 뒤 옵션을 설정하세요. 시작 (또는 +) 버튼을 누르면 해당 모드가 적용된 상태로 게임이 시작됩니다.",
    "Buttons":
        "버튼",
    "Point with the Wii Remote and press A, or move with the D-pad; in the games list, - and + turn the pages. B goes back, 2 opens Settings and HOME opens the HOME Menu. The Classic Controller and GameCube controllers work too, with the same buttons.":
        "위모컨으로 화면을 가리키고 A 버튼을 누르거나 십자 버튼으로 조작할 수 있으며, 게임 목록에서는 - 및 + 버튼으로 페이지를 넘길 수 있습니다. B 버튼은 이전 화면으로 돌아가고, 2 버튼은 설정 메뉴를, 홈 버튼은 홈 메뉴를 엽니다. 클래식 컨트롤러나 게임큐브 컨트롤러도 사용할 수 있으며, 버튼 기능은 동일합니다.",
    "You're all set":
        "준비 완료",
    "Settings has the video, language, online and update options. For more help, see the guide on RiftWii's GitHub page or join the Discord. Settings > Tutorial shows this tour again.":
        "설정 메뉴에는 비디오, 언어, 온라인, 업데이트 관련 옵션이 있습니다. 더 자세한 내용은 RiftWii GitHub 페이지의 가이드를 참조하거나 Discord에 참여해 확인하세요. '설정 > 자습서'를 선택하면 이 안내를 다시 볼 수 있습니다.",
    "Next":
        "다음",
    "Skip":
        "건너뛰기",
    "Let's go":
        "시작하기",
    "Tutorial":
        "자습서",
    "Show":
        "표시",
    "The short tour of RiftWii's basics that a new SD card starts with.":
        "새 SD 카드로 시작할 때 진행되는 RiftWii의 기본 기능에 대한 간략한 안내입니다.",
    "Credits and license":
        "크레딧 및 라이선스",
    "View":
        "보기",
    "Who RiftWii's parts come from, its license (the GNU GPL, version 3 or later) and where its source is.":
        "RiftWii의 구성 요소 출처, 라이선스(GNU GPL 버전 3 이상), 소스 코드 위치.",
    "Experimental. With the adapter plugged in when a game starts, its controllers fill the empty ports in games that take a GameCube controller. Needs IOS 58 or a d2x cIOS.":
        "실험적 기능입니다. 게임을 시작할 때 어댑터를 연결하면, 게임큐브 컨트롤러를 지원하는 게임에서 컨트롤러가 연결되지 않은 포트를 해당 어댑터의 컨트롤러가 채우게 됩니다. 이 기능을 사용하려면 IOS 58 또는 d2x cIOS가 필요합니다.",
    "Experimental. Always on, even with no adapter plugged in, so it can be plugged in during a game. It needs IOS 58 or a d2x cIOS.":
        "실험적인 기능입니다. 어댑터가 연결되어 있지 않아도 항상 활성화된 상태로 유지되므로 게임 도중에도 어댑터를 연결할 수 있습니다. IOS 58 또는 d2x cIOS가 필요합니다.",
    "Experimental. The adapter is left alone.":
        "실험적 기능입니다. 어댑터는 그대로 유지됩니다.",
    "Adapter: working":
        "어답터：작업중",
    "Adapter: starting...":
        "어답터：시작중...",
    "Adapter: another program is using it, waiting":
        "어답터：다른 프로그램에서 사용 중이고, 대기 중",
    "Adapter: it did not answer ({1}), trying again":
        "어답터：({1})에 응답하지 않음, 다시 시도",
    "Adapter: not found. Plug in its black USB plug.":
        "어댑터: 찾을 수 없습니다. 검은색 USB 플러그를 연결하세요.",
    "Press buttons on a controller in the adapter to see them here. In a game that supports the GameCube controller, the adapter's controllers fill the ports that have none plugged in.":
        "어댑터에 연결된 컨트롤러의 버튼을 누르면 이곳에 표시됩니다. 게임큐브 컨트롤러를 지원하는 게임에서는 어댑터에 연결된 컨트롤러가 비어 있는 포트를 채우게 됩니다.",
    "This IOS has no USB HID (IOS{1}). Choose IOS 58 or a d2x cIOS as the Menu IOS.":
        "이 IOS에는 USB HID (IOS{1}) 기능이 없습니다. 메뉴 IOS로 IOS 58 또는 d2x cIOS를 선택하세요.",
    "Port {1}":
        "포트 {1}",
    "nothing plugged in":
        "연결되지 않음",
    "The menu's language. Wii follows the console's own setting.":
        "메뉴 언어입니다. Wii는 본체 자체 설정을 따릅니다.",
    "Downloads the newest game names from GameTDB.":
        "GameTDB에서 최신 게임 이름을 다운로드합니다.",
    "Shows live what the controllers in the adapter are pressing.":
        "어댑터에 연결된 컨트롤러의 입력 상태를 실시간으로 보여줍니다.",
    "Reads the SD card and the USB drive again.":
        "SD 카드와 USB 드라이브를 다시 읽습니다.",
    "No d2x cIOS was found in slots 248 to 252, so the menu runs under IOS 58. Install d2x to play games from SD or USB.":
        "슬롯 248~252에서 d2x cIOS가 발견되지 않아 메뉴가 IOS 58로 실행됩니다. SD 카드나 USB로 게임을 플레이하려면 d2x를 설치하세요.",
    "Check for a new version":
        "새 버전 확인",
    "Check":
        "확인",
    "This is RiftWii {1}. Looks on GitHub for a newer release.":
        "이것은 RiftWii {1}입니다. GitHub에서 더 최신 릴리스를 확인해 보세요.",
    "Asking GitHub...":
        "GitHub에 요청 중...",
    "RiftWii {1} is out: {2}":
        "RiftWii {1} 출시: {2}",
    "RiftWii {1} is the newest version.":
        "RiftWii {1}이(가) 최신 버전입니다.",
    "Could not check: {1}":
        "확인할 수 없음: {1}",
    "Favourites":
        "즐겨찾기",
    "Favourite":
        "즐겨찾기",
    "Favourites have their own view on Home: press 1 there until it shows.":
        "홈 화면에서 즐겨찾기에 대한 별도의 보기가 제공됩니다. 해당 화면이 나타날 때까지 '1'번을 길게 누르세요.",
    "No favourite is on these drives. Mark games on their page. Press 1 for all games.":
        "이 드라이브에는 즐겨찾기가 없습니다. 각 게임 페이지에서 게임을 즐겨찾기로 표시하세요. 모든 게임을 보려면 1번을 누르세요.",
    "Could not save the settings to the SD card.":
        "SD 카드에 설정을 저장할 수 없었습니다.",
    "Video mode":
        "비디오 모드",
    "Game language":
        "게임 언어",
    "The console's":
        "본체 설정",
    "Automatic":
        "자동",
    "Japanese":
        "일본어",
    "English":
        "영어",
    "German":
        "독일어",
    "French":
        "프랑스어",
    "Spanish":
        "스페인어",
    "Italian":
        "이탈리아어",
    "Dutch":
        "네덜란드어",
    "Chinese (simplified)":
        "중국어 (간체)",
    "Chinese (traditional)":
        "중국어 (번체)",
    "Korean":
        "한국어",
    "The TV signal the game sends. PAL 50 Hz needs a TV that takes it, 480p a component cable.":
        "게임이 출력하는 TV 신호입니다. PAL 50Hz 방식은 이를 지원하는 TV가 필요하며, 480p는 컴포넌트 케이블이 필요합니다.",
    "The language the game is told the console uses. Pick one the game has: some games stop without it.":
        "게임이 콘솔에서 사용한다고 인식하는 언어입니다. 게임이 지원하는 언어 중 하나를 선택하세요. 일부 게임은 이를 설정하지 않으면 실행이 중단될 수 있습니다.",
    "The d2x cIOS the game runs under. Automatic uses the menu's, else the first of 249, 250 and 251 that works.":
        "게임이 실행될 d2x cIOS입니다. '자동'으로 설정하면 메뉴의 설정을 따르며, 그렇지 않은 경우 249, 250, 251번 중 정상적으로 작동하는 첫 번째 cIOS가 사용됩니다.",
    "Online server":
        "온라인 서버",
    "Home tiles":
        "홈 타이트",
    "Covers":
        "커버",
    "Names":
        "이름",
    "Shelf":
        "선반",
    "Channels":
        "채널",
    "Widescreen menu":
        "와이드화면 메뉴",
    "Screen size":
        "화면 크기",
    "On a 16:9 TV the menu is drawn narrower, so covers and pictures keep their shape. Automatic follows the Wii's own TV setting.":
        "16:9 비율의 TV에서는 메뉴가 더 좁게 표시되므로 표지와 사진의 형태가 유지됩니다. '자동' 설정은 Wii 본체의 TV 설정 방식을 따릅니다.",
    "Makes the menu smaller on screen, so nothing is cut off at the TV's edges. Lower it until the whole menu shows.":
        "화면상의 메뉴 크기를 줄여 TV 가장자리에서 메뉴가 잘리지 않도록 합니다. 메뉴 전체가 보일 때까지 크기를 낮추세요.",
    "Covers: each game's box art from GameTDB (fetched when downloads are on). Shelf: the boxes on a shelf. Channels: each game's animated icon, and its banner when you pick it, as on the Wii Menu. Names: the names only.":
        "'커버'는 다운로드 기능이 켜진 상태에서 홈 메뉴가 열려 있을 때 GameTDB에서 가져온 각 게임의 패키지 이미지를 표시합니다. '선반'은 게임 패키지를 선반 위에 세워진 형태로 보여줍니다. '채널'은 Wii 메뉴와 마찬가지로 각 게임의 고유한 애니메이션 아이콘을 표시하며, 게임을 선택하면 배너가 나타납니다. '이름'는 게임 이름만 표시합니다.",
    "Custom (no wfc_domain set)":
        "커스텀 (wfc_domain 설정되지 않음)",
    "The online server the game uses in place of Nintendo's, which closed. Custom uses wfc_domain in settings.txt.":
        "서비스가 종료된 닌텐도 서버 대신 이 게임이 사용하는 온라인 서버입니다. 설정 파일(settings.txt)에서 wfc_domain을 사용합니다.",
    "Getting covers from GameTDB: {1} left":
        "GameTDB에서 커버 가져오는 중: {1} 개 남음",
    "Covers could not be downloaded ({1}). To try again, use Settings > Look for games again.":
        "커버 이미지를 ({1})에서 다운로드할 수 없습니다. 다시 시도하려면 설정 > 게임 다시 찾기를 이용하세요.",
    "Cover":
        "커버",
    "Download again":
        "다시 다운로드",
    "Downloads this game's box art from GameTDB now.":
        "지금 GameTDB에서 이 게임의 박스 아트를 다운로드하세요.",
    "Downloading the cover...":
        "커버 다운로드 중...",
    "Cover downloaded.":
        "커버가 다운로드되었습니다.",
    "GameTDB has no cover for this game.":
        "GameTDB에 이 게임의 표지 이미지가 없습니다.",
    "Could not download the cover: {1}":
        "커버를 다운로드할 수 없음: {1}",
    "Menu sounds":
        "메뉴 사운드",
    "Normal":
        "보통",
    "Quiet":
        "조용함",
    "How loud the menu's clicks are. Quiet softens the tick the pointer makes moving onto something.":
        "메뉴를 클릭할 때 나는 소리의 크기입니다. '조용함' 설정은 포인터가 항목 위로 이동할 때 발생하는 '틱' 소리를 부드럽게 만들어 줍니다.",
    "Wii Menu button":
        "Wii 메뉴 버튼",
    "Back to RiftWii":
        "RiftWii로 돌아가기",
    "The Wii Menu button in a game's HOME Menu brings you back to RiftWii. It needs the RiftWii channel installed.":
        "게임 내 홈 메뉴의 'Wii 메뉴' 버튼을 누르면 RiftWii로 돌아갑니다. 이를 위해서는 RiftWii 채널이 설치되어 있어야 합니다.",
    "The Wii Menu button in a game's HOME Menu can bring you back to RiftWii once the RiftWii channel is installed (Settings).":
        "게임 내 홈 메뉴에 있는 'Wii 메뉴' 버튼을 사용하면, Rift Wii 채널이 설치된 경우(설정에서 확인 가능) Rift Wii로 돌아갈 수 있습니다.",
    "Menu music":
        "메뉴 음악",
    "In-game screenshots":
        "게임 내 스크린샷",
    "Experimental. In a game, hold 1 and press HOME (GameCube controller: hold L and R, press Down). Pictures go to sd:/riftwii/screenshots when RiftWii next starts. Some games and mods don't work with it.":
        "실험적 기능입니다. 게임 중 1번 버튼을 누른 상태에서 홈 버튼을 누르세요 (또는 게임큐브 컨트롤러의 L과 R 버튼을 누른 상태에서 아래 방향키를 누르세요). 다음에 RiftWii를 실행하면 스크린샷이 sd:/riftwii/screenshots 경로에 저장됩니다. 일부 게임이나 모드에서는 이 기능이 작동하지 않을 수 있습니다.",
    "Music while the menu is open: music.ogg from sd:/riftwii, or the one in RiftWii's own folder.":
        "메뉴가 열려 있을 때 재생되는 음악: sd:/riftwii 경로의 music.ogg 또는 RiftWii 폴더 내의 음악 파일입니다.",
    "No music.ogg found in sd:/riftwii or in RiftWii's own folder.":
        "sd:/riftwii 또는 RiftWii 자체 폴더에서 music.ogg 파일을 찾을 수 없습니다.",
    "On a Wii U the GameCube adapter may not work in game from the front USB ports; the rear ones work.":
        "Wii U에서 게임큐브 어댑터를 전면 USB 포트에 연결하면 게임 중에 작동하지 않을 수 있으나, 후면 포트에서는 정상적으로 작동합니다.",
    "Packs on USB are experimental; if it fails, copy them to SD.":
        "USB의 팩은 실험적인 기능입니다. 실패할 경우 SD 카드로 복사하세요.",
    "CTGP Revolution (a pack that starts a Homebrew Channel app) does not work from RiftWii yet: it stops on a black or green screen. Start CTGP from the Homebrew Channel instead.":
        "CTGP Revolution(홈브류 채널 앱을 통해 실행하는 팩)은 아직 RiftWii에서 정상적으로 작동하지 않으며, 검은색 또는 녹색 화면에서 멈춥니다. 대신 홈브류 채널에서 CTGP를 실행하세요.",
    "CTGP Revolution does not work from RiftWii yet (see Start).":
        "CTGP Revolution은 아직 RiftWii에서 작동하지 않습니다. (시작 항목 참조)",
    " (and {1} more)":
        " (그리고 {1} 개 더)",
    "Code builds on the USB drive won't work. Move {1} to the SD card.":
        "USB 드라이브에서 빌드된 코드는 작동하지 않습니다. {1}을(를) SD 카드로 이동하세요.",
    "This won't work: code builds like Project+ have to be on the SD card, not the USB drive. Move {1} to the same spot on your SD card and try again. Your games can stay on USB.":
        "이 방법으로는 작동하지 않습니다. 프로젝트+와 같은 커스텀 빌드는 USB 드라이브가 아닌 SD 카드에 있어야 합니다. {1}을(를) SD 카드의 동일한 위치로 옮긴 후 다시 시도해 보세요. 게임 파일은 USB에 그대로 두어도 됩니다.",
    "Code builds need the game on USB or disc, not the SD card.":
        "코드 빌드를 실행하려면 게임이 SD 카드가 아닌 USB나 디스크에 있어야 합니다.",
    "{1} is on the USB drive: the game has to be there too.":
        "{1}이(가) USB 드라이브에 있습니다: 게임도 그곳에 있어야 합니다.",
    "This won't work: {1} is on the USB drive, and RiftWii reads the USB drive during a game only when the game is on it too. Put the game on the USB drive, or put {1} in the riftwii folder on your SD card.":
        "이 방법은 작동하지 않습니다: {1} 파일이 USB 드라이브에 있는데, RiftWii는 게임 파일도 해당 USB 드라이브에 있을 때만 게임 실행 중 USB 드라이브를 읽어오기 때문입니다. 게임을 USB 드라이브로 옮기거나, SD 카드의 riftwii 폴더에 {1} 파일을 넣으세요.",
    "Updating RiftWii":
        "RiftWii 업데이트",
    "RiftWii {1} is out. Downloading and installing it now; this takes a minute...":
        "RiftWii {1}이(가) 출시되었습니다. 지금 다운로드 및 설치 중이고, 시간이 조금 걸리는 중...",
    "Update failed":
        "업데이트 실패",
    "RiftWii {1} could not be installed: {2}. This version keeps working; the new one is at {3}":
        "RiftWii {1}을(를) 설치할 수 없습니다: {2}. 현재 버전은 계속 작동하며, 새 버전은 {3}에 있습니다.",
    "OK":
        "확인",
    "RiftWii updated":
        "RiftWii 업데이트됨",
    "RiftWii {1} is installed ({2}). It runs the next time RiftWii starts. Leave to the Homebrew Channel now and start it again?":
        "RiftWii {1}이(가) ({2})에 설치되었습니다. RiftWii는 다음에 실행될 때 작동합니다. 지금 홈브류 채널로 나가서 다시 실행할까요?",
    "Leave":
        "종료",
    "Later":
        "나중에",
    "RiftWii {1} is installed. Start RiftWii again to use it.":
        "RiftWii {1}이(가) 설치되었습니다. 사용하려면 RiftWii를 다시 시작하세요.",
    "Update available":
        "업데이트 가능",
    "RiftWii {1} is out (this is {2}). Update now? It takes a minute.":
        "RiftWii {1} 버전이 출시되었습니다. (현재 버전은 {2}) 지금 업데이트할까요? 1분 정도 소요됩니다.",
    "Not now":
        "지금 안 함",
    "Are you sure?":
        "정말 확실한가요?",
    "Are you sure you don't want to update? If you had an issue, it could have been fixed in the latest update!":
        "정말 업데이트하지 않나요? 혹시 문제가 있었다면 최신 업데이트에서 해결되었을 수도 있습니다!",
    "RiftWii {1} is out. Settings > Check for a new version installs it.":
        "RiftWii {1}이(가) 출시되었습니다. 설정 > 새 버전 확인을 통해 설치할 수 있습니다.",
    "Updates":
        "업데이트",
    "Beta":
        "베타",
    "Stable":
        "안정화",
    "Beta: every new version, including test builds that may have new bugs. For testers.":
        "베타: 새로운 버그가 포함될 수 있는 테스트 빌드를 포함한 모든 새 버전입니다. 테스터용입니다.",
    "Stable: only versions marked stable, which testers have checked.":
        "안정 버전: 테스터가 확인을 마친, '안정'으로 표시된 버전만 해당합니다.",
    "No stable version is out yet. This is RiftWii {1}.":
        "아직 안정 버전은 출시되지 않았습니다. 이것은 RiftWii {1}입니다.",
    "SD card: {1}":
        "SD 카드: {1}",
    "USB drive: {1}":
        "USB 드라이브: {1}",
    "Drive problem":
        "드라이브 문제",
    "Games on that drive are not listed. Check the drive on a computer; details are in sd:/riftwii/session.log.":
        "해당 드라이브의 게임이 목록에 표시되지 않습니다. 컴퓨터에서 드라이브를 확인해 보세요오. 자세한 내용은 sd:/riftwii/session.log 파일에서 확인할 수 있습니다.",
    "SD card problems":
        "SD 카드 문제",
    "The SD card had trouble while the last game was saving. The details are in sd:/riftwii/cardlog.txt; please send that file to the RiftWii developers.":
        "마지막 게임을 저장하는 도중 SD 카드에 문제가 발생했습니다. 자세한 내용은 sd:/riftwii/cardlog.txt 파일에 기록되어 있으니, 해당 파일을 RiftWii 개발자에게 보내주세요.",
    "Before you play":
        "플레이하기 전",
    "Press Start again to play.":
        "게임을 시작하려면 시작을 다시 누르세요.",
    "Game cIOS":
        "게임 cIOS",
    "Add RiftWii to the Wii Menu?":
        "Wii 메뉴에 RiftWii를 추가할까요?",
    "RiftWii can have a channel on the Wii Menu, so it starts without the Homebrew Channel. The channel only starts RiftWii from your SD card: RiftWii's updates keep working and the channel never needs reinstalling. The channel installer opens, then brings you back here. Settings can open it again later.":
        "RiftWii는 Wii 메뉴에 채널을 생성할 수 있어 홈브류 채널을 거치지 않고도 바로 실행할 수 있습니다. 이 채널은 SD 카드의 RiftWii를 실행하는 방식이므로, RiftWii를 업데이트해도 채널을 재설치할 필요가 없습니다. 채널 설치 프로그램을 실행하면 다시 이 화면으로 돌아오게 되며, 나중에 설정 메뉴를 통해 다시 실행할 수도 있습니다.",
    "Open installer":
        "설치 프로그램 열기",
    "No thanks":
        "아니요",
    "RiftWii channel on the Wii Menu":
        "Wii 메뉴의 RiftWii 채널",
    "Installed":
        "설치됨",
    "Add":
        "추가",
    "Open the channel installer?":
        "채널 설치 프로그램을 열까요?",
    "RiftWii closes and the channel installer opens. It adds, updates or removes the RiftWii channel, then brings you back here.":
        "RiftWii가 종료되고 채널 설치 프로그램이 실행됩니다. 이 프로그램은 RiftWii 채널을 추가, 업데이트 또는 삭제한 다음 다시 이 화면으로 돌아옵니다.",
    "Open":
        "열기",
    "Cancel":
        "취소",
    "The RiftWii channel":
        "RiftWii 채널",
    "Opening the installer for":
        "설치 프로그램 열기",
    "RiftWii starts again when it is done.":
        "RiftWii는 작업이 완료되면 다시 시작됩니다.",
    "A Wii Menu channel that starts RiftWii from the SD card. It holds no copy of RiftWii, so updates keep working. Opens the channel installer, to add, update or remove it.":
        "SD 카드에서 RiftWii를 실행하는 Wii 메뉴 채널입니다. RiftWii 본체를 포함하고 있지 않으므로 업데이트가 정상적으로 작동합니다. 채널 설치 프로그램을 실행하여 채널을 추가, 업데이트 또는 삭제할 수 있습니다.",
    "Copy apps/riftwii_channel from the RiftWii zip to the SD card first":
        "먼저 RiftWii zip 파일에서 apps/riftwii_channel을 SD 카드로 복사",
    "Dolphin checks real signatures, so the channel can only be installed on a Wii":
        "돌핀은 실제 서명을 확인하므로, 해당 채널은 Wii에서만 설치할 수 있음",
    "Installing the channel needs a d2x cIOS in slot 249, 250 or 251":
        "채널을 설치하려면 슬롯 249, 250 또는 251에 d2x cIOS가 필요함",
    "(Log: sd:/riftwii/session.log)":
        "(로그: sd:/riftwii/session.log)",
    "RiftWii needs an SD card":
        "RiftWii에는 SD 카드 필요",
    "USB mode is not supported yet":
        "USB 모드는 아직 미지원",
    "No SD card was found":
        "SD 카드를 찾을 수 없음",
    "RiftWii was started from a USB drive. It keeps its settings, logs and saves on the SD card, so for now it needs one to run. Copy the sd-card folder from the RiftWii zip to a FAT32 SD card, put the card in the Wii and start RiftWii from it.":
        "RiftWii는 USB 드라이브에서 실행됩니다. 설정, 로그, 저장 데이터는 SD 카드에 기록되므로 현재 실행을 위해서는 SD 카드가 필요합니다. RiftWii 압축 파일에 포함된 'sd-card' 폴더를 FAT32 형식의 SD 카드에 복사한 뒤, 해당 카드를 Wii에 연결하고 RiftWii를 실행하세요.",
    "RiftWii keeps its settings, logs and saves on the SD card and could not read one. Put a FAT32 SD card in the Wii with the sd-card folder from the RiftWii zip on it, then start RiftWii again.":
        "RiftWii는 설정, 로그, 세이브 파일을 SD 카드에 저장하는데, 현재 SD 카드를 읽을 수 없는 상태입니다. RiftWii 압축 파일에 포함된 'sd-card' 폴더를 FAT32 형식의 SD 카드에 담아 Wii에 삽입한 후, RiftWii를 다시 실행해 보세요.",
    "Need a card? Scan this.":
        "카드가 필요한가요? 이것을 스캔하세요.",
    "Is this a burned disc?":
        "이것은 구운 디스크인가요?",
    "The drive could not read this disc. If it is a burned disc, RiftWii can read it through d2x on older Wiis (later Wii drives read only Nintendo discs). Burned discs can wear out the disc drive sooner: use them at your own risk. The menu then restarts under IOS{1} for this session.":
        "드라이브가 이 디스크를 읽을 수 없습니다. (신형 Wii 드라이브는 정품 Nintendo 디스크만 인식하지만) 구형 Wii의 경우 d2x를 통해 RiftWii로 구운 디스크를 읽을 수 있습니다. 구운 디스크는 드라이브의 수명을 단축시킬 수 있으므로 사용 시 주의가 필요합니다. 이후 해당 세션 동안 메뉴가 IOS{1} 기반으로 재시작됩니다.",
    "Burned disc: it wears the Wii's disc drive more than a pressed disc does. Play at your own risk.":
        "복사한 디스크는 정품 디스크보다 Wii의 디스크 드라이브에 더 많은 무리를 줍니다. 사용 시 발생하는 문제는 전적으로 사용자의 책임입니다.",
    "Try with d2x":
        "d2x로 시도",
    "The menu runs under IOS{1} for this session, to read burned discs. Pick the disc.":
        "이번 세션에서는 구워진 디스크를 읽기 위해 IOS{1}로 메뉴가 실행됩니다. 디스크를 선택하세요.",
    "The drive cannot read this disc, even through d2x. Later Wii drives read only Nintendo discs, never burned ones; on an older Wii, the burn may be bad.":
        "d2x를 사용하더라도 드라이브가 이 디스크를 읽을 수 없습니다. 이후에 출시된 Wii 드라이브는 닌텐도 정품 디스크만 읽을 수 있고 구운 디스크는 인식하지 못하며, 구형 Wii의 경우 디스크가 제대로 구워지지 않았을 수도 있습니다.",
    "The drive cannot read this disc. If it is a burned disc, RiftWii needs a d2x cIOS to read it.":
        "드라이브가 이 디스크를 읽을 수 없습니다. 구운 디스크인 경우, RiftWii에서 이를 읽으려면 d2x cIOS가 필요합니다.",
    "RiftWii could not restart. Start it again from the Homebrew Channel.":
        "RiftWii를 다시 시작할 수 없습니다. 홈브류 채널에서 다시 시작해 주세요.",
    "1 game":
        "게임 1 개",
    "{1} games":
        "{1} 개 게임",
    "Search \"{1}\"":
        "\"{1}\" 검색",
    "No game matches \"{1}\". Press 1 for all games.":
        "\"{1}\"에 일치하는 게임이 없습니다. 모든 게임을 보려면 1을 누르세요.",
    "1: all games":
        "1: 모든 게임",
    "Search games":
        "게임 검색",
    "Search":
        "검색",
    "Space":
        "공간",
    "Clear":
        "지우기",
    "Del":
        "삭제",
    "Homebrew Channel":
        "홈브류 채널",
    "Wii Menu":
        "Wii 메뉴",
    "Power off":
        "전원 종료",
    "HOME Menu":
        "홈 메뉴",
    "Close":
        "닫기",
    "Opens the HOME Menu, as HOME does: the Homebrew Channel, the Wii Menu, Priiloader or power off.":
        "홈 버튼과 마찬가지로 홈 메뉴(홈브류 채널, Wii 메뉴, Priiloader 또는 전원 끄기)를 엽니다.",
    "Sending a report":
        "보고서 전송",
    "Gathering the logs and sending them. This can take half a minute...":
        "로그를 수집하여 전송합니다. 이 작업은 30초 정도 걸릴 수 있습니다...",
    "Report not sent":
        "보고서가 전송되지 않았음",
    "It could not be sent: {1}. It is saved on the SD card as sd:/riftwii/report.txt: send that file instead.":
        "전송할 수 없었습니다: {1}. 해당 파일은 SD 카드에 sd:/riftwii/report.txt로 저장되었으니, 대신 이 파일을 보내주세요.",
    "It could not be sent or saved: {1}":
        "전송하거나 저장할 수 없음: {1}",
    "Send this link to whoever is helping you, or scan the code with a phone:":
        "도움을 주고 있는 분께 이 링크를 보내거나, 휴대폰으로 코드 스캔:",
    "The report was too big, so only its start was kept.":
        "보고서가 너무 커서 앞부분만 남겼습니다.",
    "Report sent":
        "보고서 보내기",
    "It holds RiftWii's logs and settings, the game's choices and packs, and which console, IOS and controllers this is. It goes to paste.rs, or dpaste.com when paste.rs can't be reached, where anyone with its link can read it.":
        "이곳에는 RiftWii의 로그와 설정, 게임 내 선택 사항 및 팩 정보, 그리고 사용 중인 콘솔, IOS, 컨트롤러 정보가 담겨 있습니다. 이 정보는 paste.rs(접속 불가 시 dpaste.com)로 전송되며, 링크를 가진 사람이라면 누구나 내용을 확인할 수 있습니다.",
    "RiftWii crashed last time":
        "지난번에 RiftWii가 충돌했음",
    "The game crashed last time":
        "지난번에 게임이 튕겼음",
    "The last launch failed":
        "마지막 실행에 실패했음",
    "Send a report of what happened?":
        "무슨 일이 있었는지에 대한 보고서를 보낼까요?",
    "Send":
        "보내기",
    "Send a problem report":
        "문제 보고서 보내기",
    "Send a problem report?":
        "문제 보고서를 보낼까요?",
    "Something went wrong? Sends what it takes to find out to a paste site, and shows a link to pass on.":
        "문제가 발생했나요? 원인을 파악하는 데 필요한 정보를 Paste Site로 전송하고, 공유할 수 있는 링크를 표시합니다.",
    "WARNING: this can freeze your Wii U":
        "경고: 이로 인해 Wii U가 멈출 수 있음",
    "On a Wii U, the GameCube adapter in games can freeze the console at 97% while a game from the SD card or a USB drive starts. It works some times and freezes others, and RiftWii cannot tell beforehand. If it freezes, hold the power button to turn the console off. Automatic is safe: it leaves the adapter out of those games.":
        "Wii U에서 SD 카드나 USB 드라이브에 있는 게임을 실행할 때 게임큐브 어댑터가 연결되어 있으면, 게임 로딩이 97% 지점에서 멈추는 현상이 발생할 수 있습니다. 어댑터가 정상적으로 작동할 때도 있고 멈출 때도 있는데, RiftWii는 이를 사전에 감지할 수 없습니다. 만약 화면이 멈춘다면 전원 버튼을 길게 눌러 본체 전원을 끄십시오. '자동' 설정을 사용하면 해당 게임 실행 시 어댑터가 활성화되지 않으므로 안전합니다.",
    "Turn it on anyway":
        "강제로 켜기",
    "Some game launches WILL freeze and need the power button. Only turn this on to test the adapter, and send a problem report when it freezes. You can set it back to Automatic here at any time.":
        "일부 게임을 실행할 때 화면이 멈춰 전원 버튼을 사용해야 할 수도 있습니다. 어댑터 테스트 용도로만 이 기능을 켜고, 멈춤 현상이 발생하면 문제 보고서를 보내주세요. 언제든지 다시 '자동'으로 설정할 수 있습니다.",
    "Yes, turn it on":
        "예, 켜기",
    "Keep it as it is":
        "그대로 두기",
    "WARNING: on this Wii U, games from the SD card or a USB drive can freeze at 97% with this on. Use Automatic unless you are testing the adapter.":
        "경고: 이 Wii U에서 이 기능을 켠 상태로 SD 카드나 USB 드라이브의 게임을 실행하면 97% 지점에서 멈출 수 있습니다. 어댑터를 테스트하는 경우가 아니라면 '자동' 설정을 사용하세요.",
    "Left as it was.":
        "변경하지 않았습니다.",
    "Theme":
        "테마",
    "Default":
        "기본",
    "The menu's colours and pictures. Themes are folders in sd:/riftwii/themes (docs/THEMES.md on GitHub).":
        "메뉴의 색상과 이미지입니다. 테마는 sd:/riftwii/themes 폴더에 위치합니다. (GitHub의 docs/THEMES.md 참조)",
    "No themes in sd:/riftwii/themes. The zip's themes folder has one to copy there.":
        "sd:/riftwii/themes 경로에 테마가 없습니다. 압축 파일 내의 themes 폴더에 해당 경로로 복사할 수 있는 테마가 하나 들어 있습니다.",
    "Restart the menu?":
        "메뉴를 다시 시작할까요?",
    "RiftWii's menu restarts to show {1}. Your games and settings stay as they are.":
        "RiftWii 메뉴가 재시작되어 {1}을(를) 표시합니다. 게임과 설정은 그대로 유지됩니다.",
    "Restart":
        "재시작",
    "Theme: {1}":
        "테마: {1}",
    "Clock":
        "시계",
    "12-hour":
        "12시간",
    "24-hour":
        "24시간",
    "How Home's clock writes the time: 12-hour (with AM and PM) or 24-hour. Automatic writes it as the menu's language does.":
        "홈 화면 시계의 시간 표시 방식: 12시간(오전/오후 표시) 또는 24시간. 자동은 메뉴 언어의 방식을 따릅니다.",
    "Menu font":
        "메뉴 글꼴",
    "Add a picture:":
        "사진 추가:",
    "The letters the menu is written in: RiftWii's own, or the Wii Menu's, read from this Wii.":
        "메뉴에 사용된 글꼴: RiftWii 자체 글꼴, 또는 해당 Wii의 Wii 메뉴 글꼴입니다.",
    "the Wii Menu's font":
        "Wii 메뉴 글꼴",
    "RiftWii's font":
        "RiftWii 글꼴",
    "Menu font: {1}":
        "메뉴 글꼴: {1}",
    "The Wii Menu's font could not be read, so RiftWii's is used.":
        "Wii 메뉴의 글꼴을 읽을 수 없어 RiftWii의 글꼴이 사용되었습니다.",
    "Files from a Mac on the SD card":
        "SD 카드에 있는 Mac의 파일",
    "This SD card has hidden files that macOS makes when it copies (names that start with \"._\"). RiftWii skips them, but they fill the card and can confuse other homebrew. To remove them, put the card in your Mac, open Terminal and type: dot_clean -m /Volumes/ followed by the card's name. On Windows, delete the files whose names start with \"._\".":
        "이 SD 카드에는 macOS가 파일을 복사할 때 생성하는 숨김 파일(이름이 \"._\"로 시작하는 파일)이 포함되어 있습니다. RiftWii는 이 파일들을 무시하고 넘어가지만, 카드 용량을 차지할 뿐만 아니라 다른 홈브류 프로그램에 혼란을 줄 수 있습니다. 이를 제거하려면 카드를 Mac에 연결한 뒤 터미널을 열고 `dot_clean -m /Volumes/`를 입력한 다음 카드 이름을 이어서 입력하십시오. 윈도우의 경우, 이름이 \"._\"로 시작하는 파일을 삭제하면 됩니다.",
    "This code mod can't start":
        "이 코드 모드는 시작되지 않음",
    "{1} has more codes than the game has room for ({2}, room for {3}). It needs the file gameconfig.txt (or gc.txt) from the same download as the mod. Copy it into the mod's own folder on the SD card, {4}, so it can't replace another mod's. Not in the download? Ask whoever made the mod.":
        "{1}에 포함된 코드의 수가 게임이 수용할 수 있는 한도({2}, 수용 한도: {3})를 초과합니다. 이 모드와 함께 제공된 gameconfig.txt (또는 gc.txt) 파일이 필요합니다. 다른 모드의 파일을 덮어쓰지 않도록, 해당 파일을 SD 카드 내 모드 전용 폴더인 {4}에 복사해 넣으세요. 만약 다운로드 파일에 해당 파일이 없다면 모드 제작자에게 문의하세요.",
    "The codes take {1}, but {2} only makes room for {3}. Turn off some cheats on this game's Cheats page, or turn off a code mod.":
        "해당 코드는 {1}을(를) 필요로 하지만, {2}은(는) {3}을(를) 위한 공간만 확보합니다. 이 게임의 치트 페이지에서 일부 치트를 끄거나 코드 모드를 비활성화하세요.",
    "Aspect ratio":
        "화면비",
    "Rumble":
        "진동",
    "Wii Remote speaker":
        "Wii 리모컨 스피커",
    "Region strings fix":
        "지역 문자열 수정",
    "Makes the game use 4:3 or widescreen 16:9 whatever the Wii's TV setting says. Not every game can be changed.":
        "Wii의 TV 설정에 맞춰 게임 화면을 4:3 또는 와이드스크린 16:9 비율로 표시하게 합니다. 단, 모든 게임에 적용할 수 있는 것은 아닙니다.",
    "Off: the Wii Remotes never rumble in this game.":
        "끔: 이 게임에서는 Wii 리모컨이 절대 진동하지 않습니다.",
    "Off: no sound from the Wii Remotes' speakers in this game.":
        "끔: 이 게임에서는 Wii 리모컨 스피커에서 소리가 나지 않습니다.",
    "For a game from another region (an import): the game sees its own region's country names where it looks for the console's.":
        "타 지역 게임(수입판)의 경우: 해당 게임은 콘솔의 지역 정보를 확인해야 할 위치에서 자사 지역의 국가명을 인식합니다.",
    "Don't show again":
        "다시 표시하지 않음",
    "Games":
        "게임",
    "Menu":
        "메뉴",
    "Finding games":
        "게임 찾기",
    "Online":
        "온라인",
    "More":
        "더 보기",
    "Picture":
        "화면",
    "Other":
        "기타",
    "Clear search":
        "검색 지우기",
    "No games found yet. Put your games (WBFS, ISO or RVZ) in a folder named wbfs or games at the top of the SD card or USB drive, then pick Look for games again in Settings.":
        "아직 게임을 찾지 못했습니다. SD 카드나 USB 드라이브 최상위 경로에 있는 'wbfs' 또는 'games' 폴더에 게임 파일(WBFS, ISO 또는 RVZ)을 넣은 다음, 설정에서 '게임 다시 찾기'를 선택하세요.",
    "This SD card ({1} GB) is too big for code builds. Make an SD image.":
        "이 SD 카드({1} GB)는 코드 빌드용으로는 용량이 너무 큽니다. SD 이미지를 생성하세요.",
    "This won't work: this SD card is bigger than 32 GB ({1} GB, SDXC), and Brawl only reads SD cards up to 32 GB, so the build's files never load. Pick \"Make an SD image...\" under the build on the Mods page: the game then reads a small card inside a file on this one. Or copy the build to a 32 GB or smaller card.":
        "이 방법은 작동하지 않습니다. 해당 SD 카드는 32GB를 초과하는 용량({1}GB, SDXC)인 반면, 'Brawl'은 최대 32GB 용량의 SD 카드만 인식하므로 빌드 파일이 로드되지 않습니다. MOD 페이지의 빌드 항목에서 \"SD 이미지 만들기...\"를 선택하세요. 그러면 게임이 실제 SD 카드 대신 해당 파일 내의 가상 SD 카드를 인식하게 됩니다. 또는 빌드 파일을 32GB 이하 용량의 SD 카드에 복사해서 사용하세요.",
    "This won't work: code builds like Project+ read the SD card while you play, so the game can't be on the SD card too. Put it on a USB drive, use the disc, or pick \"Make an SD image...\" under the build on the Mods page.":
        "이 방법은 작동하지 않습니다. 프로젝트+와 같은 빌드는 게임을 플레이하는 동안 SD 카드를 읽어 들이기 때문에, 게임 파일 자체를 SD 카드에 함께 둘 수 없습니다. 대신 USB 드라이브를 사용하거나, 디스크를 사용하거나, 또는 MOD 페이지의 해당 빌드 항목 아래에 있는 \"SD 이미지 생성...\" 기능을 선택하세요.",
    "Make an SD image...":
        "SD 이미지 만들기...",
    "RiftWii stopped while reading the Wii Menu's font last time, so it uses its own. Send a problem report so this can be fixed.":
        "지난번 RiftWii가 Wii 메뉴의 글꼴을 읽어오는 도중 멈춘 적이 있어, 현재는 자체 글꼴을 사용하고 있습니다. 이 문제가 해결될 수 있도록 오류 보고서를 보내주세요.",
    "Report sent: {1}":
        "보고서 전송됨: {1}",
    "SD image":
        "SD 이미지",
    "Looking at the build's files...":
        "빌드 파일을 살펴보는 중...",
    "No SD image made":
        "SD 이미지가 생성되지 않았음",
    " and ":
        " 그리고 ",
    "RiftWii copies {1} into sd:/riftwii/{2} ({3}). The game then gets the image as its SD card, so it can be on the SD card too. This takes about {4} minutes.":
        "RiftWii는 {1}을(를) sd:/riftwii/{2} ({3}) 경로로 복사합니다. 그러면 게임이 해당 이미지를 SD 카드로 인식하므로, 게임 파일 또한 SD 카드에 저장할 수 있게 됩니다. 이 과정은 약 {4}분 정도 소요됩니다.",
    "It is saved in {1} parts, as a FAT32 card holds no file of 4 GB.":
        "FAT32 카드는 4GB 이상의 파일을 저장할 수 없으므로 {1}개의 파일로 나누어 저장됩니다.",
    "It replaces the {1} there now.":
        "그것은 현재 그곳에 있는 {1}을(를) 대체합니다.",
    "Make an SD image?":
        "SD 이미지를 만들까요?",
    "Make it":
        "만들기",
    "Making {1}":
        "{1} 만들기",
    "Stop":
        "정지",
    "{1} minutes left":
        "{1} 분 남음",
    "{1} seconds left":
        "{1} 초 남음",
    "Free space":
        "여유 공간",
    "Stopped. Nothing was kept.":
        "중단되었습니다. 아무것도 보관되지 않았습니다.",
    "{1} could not be made: {2}. Nothing was kept.":
        "{1}을(를) 생성할 수 없습니다: {2}. 아무것도 보존되지 않았습니다.",
    "SD image made":
        "SD 이미지 생성됨",
    "{1} is in sd:/riftwii, and the build in it is turned on: the game gets the image as its SD card.":
        "{1}은(는) sd:/riftwii에 위치하며 해당 빌드가 활성화되어 있습니다. 따라서 게임은 이를 SD 카드로 인식하여 이미지를 불러옵니다.",
    "Remove from the list":
        "목록에서 제거",
    "Add a code build...":
        "코드 빌드 추가...",
    "..  (up a folder)":
        ".. (상위 폴더로)",
    "Pick":
        "선택",
    "Nothing here":
        "여기에 아무것도 없음",
    "Add a code build":
        "코드 빌드 추가",
    "Achievement unlocked: License Enthusiast":
        "업적 달성: 라이선스 열광적인 팬",
    "Congratulations! You read all 5,644 words of the GNU GPL, version 3. Are you really that bored? Respect to the developers whose work RiftWii builds on: every one of you is credited above. The GPL is there to protect people who share their code, not to be waved around only when it's handy for picking a fight. Your reward: absolutely nothing, as the license says (\"WITHOUT ANY WARRANTY\").":
        "축하합니다! GNU GPL 버전 3의 5,644개 단어를 모두 읽으셨군요. 정말 그렇게 할 일이 없으셨나요? RiftWii의 기반이 된 소프트웨어를 개발한 분들께 경의를 표합니다. 모든 개발자의 이름은 위에 명시되어 있습니다. GPL은 코드를 공유하는 사람들을 보호하기 위해 존재하는 것이지, 단지 누군가와 시비를 걸기 편할 때만 내세우라고 있는 것이 아닙니다. 당신이 받을 보상은? 라이선스에 명시된 대로 '전혀 없습니다' (\"어떠한 보증도 없음\").",
    "Fair enough":
        "알겠음",
    "Only a build in a folder can go into an image.":
        "폴더 내의 빌드만 이미지에 포함될 수 있습니다.",
    "The build cannot go into an image: {1}.":
        "빌드를 이미지로 만들 수 없습니다: {1}.",
    "The image needs {1} on the SD card and {2} is free. Make room (or use a bigger card) and try again.":
        "이미지를 저장하려면 SD 카드에 {1}의 공간이 필요하지만, 현재 {2}만 비어 있습니다. 공간을 확보하거나 더 큰 용량의 카드를 사용하여 다시 시도해 주세요.",
    "Cannot replace {1}.":
        "{1}을(를) 대체할 수 없습니다.",
    "{1} has folders too deep to copy.":
        "{1}에는 복사하기에는 경로가 너무 깊은 폴더가 포함되어 있습니다.",
    "Cannot read sd:{1}.":
        "sd:{1}을 읽을 수 없습니다.",
    "The build has more than {1} files.":
        "해당 빌드에는 {1}개 이상의 파일이 포함되어 있습니다.",
}

# French, a table of its own (msgid: text), from MidyGamy's translation
# (pull request 22). A msgid missing here shows in English.
FR = {
    "Games with mods":
        "Jeux avec mods",
    "All games":
        "Tous les jeux",
    "Recently played":
        "Joués récemment",
    "No game on these drives was played from RiftWii yet. Press 1 for all games.":
        "Aucun jeu sur ces stockages n'a encore été lancé depuis RiftWii. Appuyez sur 1 pour voir tous les jeux.",
    "{1}/{2}":
        "{2}/{1}",
    "Played once, on {1}":
        "Joué 1 fois, le {1}",
    "Played {1} times, last on {2}":
        "Joué {1} fois, la dernière fois le {2}",
    "1: view   2: settings   -/+: pages   B: A to Z":
        "1 : vue   2 : paramètres   -/+ : pages   B : A à Z",
    "Page {1} of {2}":
        "Page {1} sur {2}",
    "{1}: no games in /wbfs or /games":
        "{1} : aucun jeu dans /wbfs ou /games",
    "SD: no card":
        "SD : aucune carte",
    "No d2x cIOS in 249-251: games cannot boot yet":
        "Aucun cIOS d2x dans 249-251 : les jeux ne peuvent pas encore démarrer",
    "No game here has packs in sd:/riivolution yet. Press 1 for all games.":
        "Aucun jeu ici n'a de pack dans sd:/riivolution. Appuyez sur 1 pour voir tous les jeux.",
    "No games found (usb:/wbfs, usb:/games, sd:/wbfs, sd:/games)":
        "Aucun jeu trouvé (usb:/wbfs, usb:/games, sd:/wbfs, sd:/games)",
    "Reading the SD card...":
        "Lecture de la carte SD...",
    "Reading the USB drive... (a big drive takes a moment)":
        "Lecture du stockage USB... (un grand stockage peut prendre un moment)",
    "Reading the disc...":
        "Lecture du disque...",
    "scan failed":
        "échec de l'analyse",
    "(the menu runs under IOS {1}; USB drives need a base-58 cIOS for that, or set the menu IOS back to 58)":
        "(le menu fonctionne sous l'IOS {1}; les stockage USB nécessitent un cIOS base-58 pour cela, ou remettez l'IOS du menu sur 58)",
    "(after the failed launch RiftWii came back under IOS {1}, which cannot read the drive here; start RiftWii again from the Homebrew Channel)":
        "(après l'échec du lancement, RiftWii est revenu sous l'IOS {1}, qui ne peut pas lire le stockage ici; relancez RiftWii depuis la Chaîne Homebrew)",
    "No disc in the drive":
        "Aucun disque dans le lecteur",
    "Disc drive":
        "Lecteur de disque",
    "USB drive":
        "Stockage USB",
    "SD card":
        "Carte SD",
    "Sun":
        "Dim",
    "Mon":
        "Lun",
    "Tue":
        "Mar",
    "Wed":
        "Mer",
    "Thu":
        "Jeu",
    "Fri":
        "Ven",
    "Sat":
        "Sam",
    "{1}:{2} AM":
        "{3}:{2}",
    "{1}:{2} PM":
        "{3}:{2}",
    "{1} {2}/{3}":
        "{1} {3}/{2}",
    "MODS":
        "MODS",
    "Back":
        "Retour",
    "Start":
        "Démarrer",
    "Saves":
        "Sauvegardes",
    "On the Wii":
        "Sur la Wii",
    "SD, from Wii save":
        "SD, depuis sauvegarde Wii",
    "SD, fresh start":
        "SD, sauvegarde vierge",
    "Kept by the pack":
        "Géré par le pack",
    "This pack keeps its own saves. Turn it off to choose here.":
        "Ce pack gère ses propres sauvegardes. Désactivez-le pour choisir ici.",
    "Saves go to the SD card, starting from the Wii's save.":
        "Les sauvegardes vont sur la carte SD, en partant de la sauvegarde Wii.",
    "Saves go to the SD card, starting fresh.":
        "Les sauvegardes vont sur la carte SD, sauvegarde vierge.",
    "Saves stay on the Wii, as usual.":
        "Les sauvegardes restent sur la Wii, comme d'habitude.",
    "Broken":
        "Endommagé",
    "On":
        "Activé",
    "Off":
        "Désactivé",
    "Mods":
        "Mods",
    "None":
        "Aucun",
    "{1} switched on":
        "{1} activé(s)",
    "On: {1}":
        "Activé : {1}",
    "No mods for this game.":
        "Aucun mod pour ce jeu.",
    "No mods on the SD card. Put Riivolution XML in sd:/riivolution.":
        "Aucun mod sur la carte SD. Placez le fichier XML Riivolution dans sd:/riivolution.",
    "1 mod pack for this game. Press A to turn it on.":
        "1 pack de mod pour ce jeu. Appuyez sur A pour l'activer.",
    "{1} mod packs for this game. Press A to turn them on.":
        "{1} packs de mods pour ce jeu. Appuyez sur A pour les activer.",
    "No mods on the SD card":
        "Aucun mod sur la carte SD",
    "No mods for this game":
        "Aucun mod pour ce jeu",
    "Put Riivolution XML in sd:/riivolution":
        "Placez le fichier XML Riivolution dans sd:/riivolution",
    "1 XML file is for another game":
        "1 fichier XML est destiné à un autre jeu",
    "{1} XML files are for other games":
        "{1} fichiers XML sont destinés à d'autres jeux",
    "Package scan failed; go back and try again":
        "Échec de l'analyse des fichiers; revenez en arrière et réessayez",
    "This XML cannot be read; the error is listed under it.":
        "Ce fichier XML ne peut pas être lu; l'erreur est indiquée ci-dessous.",
    "This XML cannot be read; fix it on the card and come back.":
        "Ce fichier XML ne peut pas être lu; corrigez-le sur la carte et réessayez.",
    "This pack cannot be turned on.":
        "Ce pack ne peut pas être activé.",
    "Off. A turns it on.":
        "Désactivé. A permet de l'activer.",
    "Off. A turns it on and shows its setting.":
        "Désactivé. Le bouton A l'active et affiche son paramètre.",
    "Off. A turns it on and shows its {1} settings.":
        "Désactivé. Le bouton A l'active et affiche ses {1} paramètres.",
    "On. It applies as a whole.":
        "Activé. S'applique dans son ensemble.",
    "On, but nothing chosen yet: pick its settings below.":
        "Activé, mais rien n'est choisi : sélectionnez ses paramètres ci-dessous.",
    "On, {1} of {2} settings chosen.":
        "Activé, {1} sur {2} paramètres choisis.",
    "No game is selected; go back and pick one.":
        "Aucun jeu n'est sélectionné; revenez en arrière et choisissez-en un.",
    "Preparing the mods...":
        "Préparation des mods...",
    "Cheats":
        "Codes de triche",
    "Picture width":
        "Largeur d'image",
    "Deflicker":
        "Anti-scintillement",
    "Black borders":
        "Bandes noires",
    "Region video fix":
        "Correction de la région vidéo",
    "For a US or Japanese game that shows no picture on a console from another region: the game is told the video hardware matches its region.":
        "Pour un jeu américain ou japonais qui n'affiche pas d'image sur une console d'une autre région : le jeu interprète que le matériel vidéo correspond à sa région.",
    "Framebuffer":
        "Tampon d'image",
    "704 pixels":
        "704 pixels",
    "720 pixels (full)":
        "720 pixels (complet)",
    "Game's own":
        "Par défaut du jeu",
    "Off (sharp)":
        "Désactivé (net)",
    "Low":
        "Faible",
    "Medium":
        "Moyen",
    "High":
        "Élevé",
    "Remove":
        "Supprimer",
    "Keep":
        "Conserver",
    "Remove all (experimental)":
        "Tout supprimer (expérimental)",
    "Remove all":
        "Tout supprimer",
    "Default ({1})":
        "Par défaut ({1})",
    "On, none picked":
        "Activé, aucun sélectionné",
    "On, {1} picked":
        "Activé, {1} sélectionné(s)",
    "Cheat codes for this game. Press A to choose them.":
        "Codes de triche pour ce jeu. Appuyez sur A pour les choisir.",
    "How wide the picture is drawn. 720 fills the screen from side to side.":
        "Largeur d'affichage de l'image. 720 remplit l'écran d'un côté à l'autre.",
    "A filter that softens the picture to hide flicker. Off gives the sharpest picture.":
        "Un filtre qui adoucit l'image pour masquer le scintillement. Désactivé donne l'image la plus nette.",
    "Remove stretches the picture over the bars at the sides. Remove all also stretches it over the bars at the top and bottom: experimental, some games show a broken picture or crash with it.":
        "\"Supprimer\" étire l'image sur les bandes latérales. \"Tout supprimer\" l'étire aussi en haut et en bas : expérimental, certains jeux peuvent afficher une image déformée ou planter.",
    "Remove takes away the bars at the sides; Remove all also the top and bottom (experimental).":
        "\"Supprimer\" retire les bandes latérales; \"Tout supprimer\" retire aussi le haut et le bas (expérimental).",
    "Last time, this game left black borders on all sides.":
        "La dernière fois, ce jeu a laissé des bandes noires de tous les côtés.",
    "Last time, this game left black borders at the sides.":
        "La dernière fois, ce jeu a laissé des bandes noires sur les côtés.",
    "Last time, this game left black borders at the top and bottom.":
        "La dernière fois, ce jeu a laissé des bandes noires en haut et en bas.",
    "Last time, this game filled the whole screen.":
        "La dernière fois, ce jeu a rempli tout l'écran.",
    "Use cheats":
        "Utiliser les codes de triche",
    "Get the latest cheats":
        "Obtenir les derniers codes de triche",
    "Download cheats":
        "Télécharger les codes de triche",
    "Download":
        "Télécharger",
    "Edit first":
        "Éditer d'abord",
    "The cheats are in {1}. Edit it on a computer to add your own.":
        "Les codes sont dans {1}. Modifiez ce fichier sur un ordinateur pour ajouter les vôtres.",
    "Downloading cheats...":
        "Téléchargement des codes de triche...",
    "{1} cheats. Turn on the ones you want.":
        "{1} codes de triche. Activez ceux que vous souhaitez.",
    "Could not download cheats: {1}":
        "Impossible de télécharger les codes de triche : {1}",
    "No cheats found online for this game.":
        "Aucun code de triche trouvé en ligne pour ce jeu.",
    "Cheats are only applied when this is On.":
        "Les codes de triche ne s'appliquent que si cette option est activée.",
    "Downloads are off in Settings.":
        "Les téléchargements sont désactivés dans les paramètres.",
    "No game is selected.":
        "Aucun jeu sélectionné.",
    "No cheat file yet. Choose Download to get one.":
        "Aucun fichier de triche. Choisissez Télécharger pour en obtenir un.",
    "No cheat file at {1}":
        "Aucun fichier de triche dans {1}",
    "The cheat file has no cheats in it: {1}":
        "Le fichier ne contient aucun code de triche : {1}",
    "Settings":
        "Paramètres",
    "Language":
        "Langue",
    "Wii: {1}":
        "Wii : {1}",
    "Download names and cheats":
        "Télécharger noms et codes de triche",
    "Get the latest game names":
        "Obtenir les derniers noms de jeux",
    "Update":
        "Mettre à jour",
    "Menu IOS":
        "IOS du menu",
    "Menu IOS: IOS 58 (no d2x cIOS found)":
        "IOS du menu : IOS 58 (aucun cIOS d2x trouvé)",
    "Find network packs (RiiFS)":
        "Trouver les packs réseau (RiiFS)",
    "Copy network packs again":
        "Recopier les packs réseau",
    "Resync":
        "Ressynchroniser",
    "Look for games again":
        "Rechercher à nouveau les jeux",
    "Rescan":
        "Réanalyser",
    "Leave RiftWii":
        "Quitter RiftWii",
    "Exit":
        "Quitter",
    "These apply to every game. A game's own page can change them for that game.":
        "S'applique à tous les jeux. La page propre à chaque jeu permet de les modifier.",
    "Game names follow the language when they are downloaded.":
        "Les noms des jeux suivent la langue sélectionnée lors de leur téléchargement.",
    "Game names and cheats are downloaded when the Wii is online.":
        "Les noms de jeux et les codes sont téléchargés lorsque la Wii est en ligne.",
    "Nothing is downloaded. Names and cheats already on the card are still used.":
        "Rien n'est téléchargé. Les noms et codes déjà présents sur la carte restent utilisés.",
    "Downloads are off. Turn on Download names and cheats first.":
        "Téléchargements désactivés. Activez d'abord le téléchargement des noms et codes.",
    "Downloading game names...":
        "Téléchargement des noms de jeux...",
    "Game names updated.":
        "Noms de jeux mis à jour.",
    "Could not download game names: {1}":
        "Impossible de télécharger les noms de jeux : {1}",
    "Cannot write sd:/riftwii/settings.txt":
        "Écriture impossible dans sd:/riftwii/settings.txt",
    "Cannot write sd:/riftwii/menu_ios.txt":
        "Écriture impossible dans sd:/riftwii/menu_ios.txt",
    "The menu runs under the Homebrew Channel's IOS (the default).":
        "Le menu s'exécute sous l'IOS de la Chaîne Homebrew (par défaut).",
    "The menu and every game run under cIOS {1}, so a cIOS with fakemote makes USB DS3/DS4 pads work as Wii Remotes. USB drives in the menu need a base-58 cIOS.":
        "Le menu et chaque jeu tournent sous le cIOS {1}. Un cIOS avec fakemote permet d'utiliser des manettes DS3/DS4 USB comme Wiimotes. Les stockages USB nécessitent un cIOS base-58.",
    "Takes effect the next time RiftWii starts.":
        "Prendra effet au prochain démarrage de RiftWii.",
    "Looks for a PC running a RiiFS server when the games are read. Rescan to look now.":
        "Cherche un PC exécutant un serveur RiiFS lors de la lecture des jeux. Réanalysez pour chercher maintenant.",
    "Only servers named by <network> in an XML on the card are used.":
        "Seuls les serveurs désignés par <network> dans un fichier XML sur la carte sont utilisés.",
    "The next launch copies every file of its network packs again.":
        "Le prochain lancement recopiera tous les fichiers des packs réseau.",
    "Starting":
        "Démarrage",
    "Dumping files from":
        "Extraction des fichiers depuis",
    "The game takes over the screen when it is ready.":
        "Le jeu prend le contrôle de l'écran lorsqu'il est prêt.",
    "Opening the game...":
        "Ouverture du jeu...",
    "GameCube adapter":
        "Adaptateur GameCube",
    "Check the GameCube adapter":
        "Vérifier l'adaptateur GameCube",
    "Test":
        "Tester",
    "Welcome to RiftWii":
        "Bienvenue dans RiftWii",
    "RiftWii starts your Wii games with Riivolution-format mods, from the disc, a USB drive or the SD card. Your game files are never changed. This short tour shows the basics.":
        "RiftWii lance vos jeux Wii avec des mods au format Riivolution, depuis le disque, un stockage USB ou la carte SD. Vos jeux ne sont jamais modifiés. Ce court guide présente les bases.",
    "Your games":
        "Vos jeux",
    "Put games in the wbfs or games folder at the top of the SD card or the USB drive (WBFS, ISO or RVZ). A disc in the drive shows up too. Home lists games that have mods first: press 1, or the round button at the bottom left, to see all your games.":
        "Placez vos jeux dans le dossier wbfs ou games à la racine de la carte SD ou du stockage USB (format WBFS, ISO ou RVZ). Les disques insérés apparaissent aussi. L'accueil liste en premier les jeux moddés : appuyez sur 1 pour tout voir.",
    "Put mod packs (the XML file and the folders that come with it) in sd:/riivolution or usb:/riivolution. Pick a game, open Mods, switch a pack on and choose its options. Start (or +) plays the game with them.":
        "Placez les packs de mods (fichier XML et dossiers associés) dans sd:/riivolution ou usb:/riivolution. Choisissez un jeu, ouvrez Mods, activez un pack et ajustez ses options. Démarrer (ou +) lance le jeu.",
    "Buttons":
        "Boutons",
    "Point with the Wii Remote and press A, or move with the D-pad; in the games list, - and + turn the pages. B goes back, 2 opens Settings and HOME opens the HOME Menu. The Classic Controller and GameCube controllers work too, with the same buttons.":
        "Pointez avec la Wiimote et appuyez sur A, ou déplacez-vous avec la croix directionnelle. - et + tournent les pages. B revient en arrière, 2 ouvre les paramètres et HOME ouvre le menu HOME. Les manettes Classique et GameCube sont aussi compatibles.",
    "You're all set":
        "Vous êtes prêt",
    "Settings has the video, language, online and update options. For more help, see the guide on RiftWii's GitHub page or join the Discord. Settings > Tutorial shows this tour again.":
        "Les paramètres contiennent les options vidéo, de langue, en ligne et de mise à jour. Pour plus d'aide, consultez le guide GitHub de RiftWii ou le Discord. Paramètres > Tutoriel réaffiche ce guide.",
    "Next":
        "Suivant",
    "Skip":
        "Passer",
    "Let's go":
        "C'est parti",
    "Tutorial":
        "Tutoriel",
    "Show":
        "Afficher",
    "The short tour of RiftWii's basics that a new SD card starts with.":
        "Le court tutoriel de présentation des fonctionnalités de base de RiftWii.",
    "Credits and license":
        "Crédits et licence",
    "View":
        "Voir",
    "Who RiftWii's parts come from, its license (the GNU GPL, version 3 or later) and where its source is.":
        "L'origine des composants de RiftWii, sa licence (GNU GPL v3 ou ultérieure) et l'emplacement du code source.",
    "Experimental. With the adapter plugged in when a game starts, its controllers fill the empty ports in games that take a GameCube controller. Needs IOS 58 or a d2x cIOS.":
        "Expérimental. Si l'adaptateur est branché au lancement, ses manettes occupent les ports libres pour les jeux GameCube. Nécessite l'IOS 58 ou un cIOS d2x.",
    "Experimental. Always on, even with no adapter plugged in, so it can be plugged in during a game. It needs IOS 58 or a d2x cIOS.":
        "Expérimental. Toujours actif pour pouvoir brancher l'adaptateur en cours de jeu. Nécessite l'IOS 58 ou un cIOS d2x.",
    "Experimental. The adapter is left alone.":
        "Expérimental. L'adaptateur n'est pas utilisé.",
    "Adapter: working":
        "Adaptateur : fonctionnel",
    "Adapter: starting...":
        "Adaptateur : démarrage...",
    "Adapter: another program is using it, waiting":
        "Adaptateur : utilisé par un autre programme, en attente",
    "Adapter: it did not answer ({1}), trying again":
        "Adaptateur : pas de réponse ({1}), nouvel essai",
    "Adapter: not found. Plug in its black USB plug.":
        "Adaptateur : introuvable. Branchez sa prise USB noire.",
    "Press buttons on a controller in the adapter to see them here. In a game that supports the GameCube controller, the adapter's controllers fill the ports that have none plugged in.":
        "Appuyez sur les boutons d'une manette reliée à l'adaptateur pour les tester ici. Dans un jeu compatible GameCube, ses manettes occupent les ports vides.",
    "This IOS has no USB HID (IOS{1}). Choose IOS 58 or a d2x cIOS as the Menu IOS.":
        "Cet IOS ne gère pas le USB HID (IOS{1}). Choisissez l'IOS 58 ou un cIOS d2x comme IOS du menu.",
    "Port {1}":
        "Port {1}",
    "nothing plugged in":
        "rien de branché",
    "The menu's language. Wii follows the console's own setting.":
        "Langue du menu. \"Wii\" utilise le paramètre de la console.",
    "Downloads the newest game names from GameTDB.":
        "Télécharge les derniers noms de jeux depuis GameTDB.",
    "Shows live what the controllers in the adapter are pressing.":
        "Affiche en direct les entrées des manettes branchées sur l'adaptateur.",
    "Reads the SD card and the USB drive again.":
        "Relaance la lecture de la carte SD et du stockage USB.",
    "No d2x cIOS was found in slots 248 to 252, so the menu runs under IOS 58. Install d2x to play games from SD or USB.":
        "Aucun cIOS d2x trouvé dans les slots 248 à 252, le menu tourne sous l'IOS 58. Installez d2x pour jouer depuis la SD ou l'USB.",
    "Check for a new version":
        "Vérifier les mises à jour",
    "Check":
        "Vérifier",
    "This is RiftWii {1}. Looks on GitHub for a newer release.":
        "Vous utilisez RiftWii {1}. Recherche une version plus récente sur GitHub.",
    "Asking GitHub...":
        "Interrogation de GitHub...",
    "RiftWii {1} is out: {2}":
        "RiftWii {1} est disponible : {2}",
    "RiftWii {1} is the newest version.":
        "RiftWii {1} est la version la plus récente.",
    "Could not check: {1}":
        "Vérification impossible : {1}",
    "Favourites":
        "Favoris",
    "Favourite":
        "Favori",
    "Favourites have their own view on Home: press 1 there until it shows.":
        "Les favoris ont leur propre vue à l'accueil : appuyez sur 1 jusqu'à la faire apparaître.",
    "No favourite is on these drives. Mark games on their page. Press 1 for all games.":
        "Aucun favori sur ces stockages. Marquez des jeux depuis leur page. Appuyez sur 1 pour tous les jeux.",
    "Could not save the settings to the SD card.":
        "Impossible d'enregistrer les paramètres sur la carte SD.",
    "Video mode":
        "Mode vidéo",
    "Game language":
        "Langue du jeu",
    "The console's":
        "Celle de la console",
    "Automatic":
        "Automatique",
    "Japanese":
        "Japonais",
    "English":
        "Anglais",
    "German":
        "Allemand",
    "French":
        "Français",
    "Spanish":
        "Espagnol",
    "Italian":
        "Italien",
    "Dutch":
        "Néerlandais",
    "Chinese (simplified)":
        "Chinois (simplifié)",
    "Chinese (traditional)":
        "Chinois (traditionnel)",
    "Korean":
        "Coréen",
    "The TV signal the game sends. PAL 50 Hz needs a TV that takes it, 480p a component cable.":
        "Signal TV émis par le jeu. Le PAL 50 Hz exige un téléviseur compatible, le 480p un câble YUV (Composante).",
    "The language the game is told the console uses. Pick one the game has: some games stop without it.":
        "Langue de console transmise au jeu. Choisissez une langue gérée par le jeu pour éviter tout blocage.",
    "The d2x cIOS the game runs under. Automatic uses the menu's, else the first of 249, 250 and 251 that works.":
        "cIOS d2x utilisé pour lancer le jeu. \"Automatique\" utilise celui du menu, ou le premier fonctionnel parmi 249, 250 et 251.",
    "Online server":
        "Serveur en ligne",
    "Home tiles":
        "Tuiles de l'accueil",
    "Covers":
        "Jacquettes",
    "Names":
        "Noms",
    "Shelf":
        "Étagère",
    "Channels":
        "Chaînes",
    "Widescreen menu":
        "Menu 16:9",
    "Screen size":
        "Taille de l'écran",
    "On a 16:9 TV the menu is drawn narrower, so covers and pictures keep their shape. Automatic follows the Wii's own TV setting.":
        "Sur une TV 16:9, le menu est ajusté pour conserver le ratio des images. \"Automatique\" suit le réglage TV de la Wii.",
    "Makes the menu smaller on screen, so nothing is cut off at the TV's edges. Lower it until the whole menu shows.":
        "Réduit l'affichage pour éviter que les bords ne soient coupés sur l'écran. Réduisez la valeur jusqu'à tout voir.",
    "Covers: each game's box art from GameTDB (fetched when downloads are on). Shelf: the boxes on a shelf. Channels: each game's animated icon, and its banner when you pick it, as on the Wii Menu. Names: the names only.":
        "Jacquettes : affiche la boîte GameTDB. Étagère : boîtes alignées sur une étagère. Chaînes : icônes et bannières animées comme sur le menu Wii. Noms : liste textuelle.",
    "Custom (no wfc_domain set)":
        "Personnalisé (wfc_domain non défini)",
    "The online server the game uses in place of Nintendo's, which closed. Custom uses wfc_domain in settings.txt.":
        "Serveur de remplacement pour le jeu en ligne officiel désormais fermé. L'option personnalisée utilise wfc_domain dans settings.txt.",
    "Getting covers from GameTDB: {1} left":
        "Récupération des jacquettes GameTDB : {1} restante(s)",
    "Covers could not be downloaded ({1}). To try again, use Settings > Look for games again.":
        "Impossible de télécharger les jacquettes ({1}). Pour réessayer : Paramètres > Rechercher à nouveau les jeux.",
    "Cover":
        "Jacquette",
    "Download again":
        "Re-télécharger",
    "Downloads this game's box art from GameTDB now.":
        "Télécharge la jacquette de ce jeu depuis GameTDB.",
    "Downloading the cover...":
        "Téléchargement de la jacquette...",
    "Cover downloaded.":
        "Jacquette téléchargée.",
    "GameTDB has no cover for this game.":
        "GameTDB ne possède aucune jacquette pour ce jeu.",
    "Could not download the cover: {1}":
        "Impossible de télécharger la jacquette : {1}",
    "Menu sounds":
        "Sons du menu",
    "Normal":
        "Normal",
    "Quiet":
        "Discret",
    "How loud the menu's clicks are. Quiet softens the tick the pointer makes moving onto something.":
        "Volume des clics du menu. \"Discret\" adoucit le bruit lors du survol des éléments.",
    "Wii Menu button":
        "Bouton Menu Wii",
    "Back to RiftWii":
        "Retour à RiftWii",
    "The Wii Menu button in a game's HOME Menu brings you back to RiftWii. It needs the RiftWii channel installed.":
        "Le bouton \"Menu Wii\" dans le menu HOME d'un jeu vous ramène à RiftWii. Nécessite l'installation de la chaîne RiftWii.",
    "The Wii Menu button in a game's HOME Menu can bring you back to RiftWii once the RiftWii channel is installed (Settings).":
        "Le bouton \"Menu Wii\" du menu HOME permet de retourner à RiftWii si la chaîne RiftWii est installée (voir Paramètres).",
    "Menu music":
        "Musique du menu",
    "In-game screenshots":
        "Captures d'écran en jeu",
    "Experimental. In a game, hold 1 and press HOME (GameCube controller: hold L and R, press Down). Pictures go to sd:/riftwii/screenshots when RiftWii next starts. Some games and mods don't work with it.":
        "Expérimental. En jeu, maintenez 1 et appuyez sur HOME (Manette GC : L + R + Bas). Les images vont dans sd:/riftwii/screenshots au relancement de RiftWii. Incompatible avec certains jeux/mods.",
    "Music while the menu is open: music.ogg from sd:/riftwii, or the one in RiftWii's own folder.":
        "Musique du menu : fichier music.ogg situé dans sd:/riftwii ou dans le dossier propre de RiftWii.",
    "No music.ogg found in sd:/riftwii or in RiftWii's own folder.":
        "Aucun fichier music.ogg trouvé dans sd:/riftwii ou dans le dossier de RiftWii.",
    "On a Wii U the GameCube adapter may not work in game from the front USB ports; the rear ones work.":
        "Sur Wii U, l'adaptateur GameCube peut ne pas fonctionner sur les ports USB avant; utilisez les ports arrière.",
    "Packs on USB are experimental; if it fails, copy them to SD.":
        "Les packs sur USB sont expérimentaux; en cas de problème, copiez-les sur la carte SD.",
    "CTGP Revolution (a pack that starts a Homebrew Channel app) does not work from RiftWii yet: it stops on a black or green screen. Start CTGP from the Homebrew Channel instead.":
        "CTGP Revolution ne fonctionne pas encore depuis RiftWii (blocage sur écran noir ou vert). Lancez-le directement depuis la Chaîne Homebrew.",
    "CTGP Revolution does not work from RiftWii yet (see Start).":
        "CTGP Revolution ne fonctionne pas encore depuis RiftWii (voir Démarrer).",
    " (and {1} more)":
        " (et {1} autre(s))",
    "Code builds on the USB drive won't work. Move {1} to the SD card.":
        "Les builds de code sur USB ne fonctionnent pas. Déplacez {1} sur la carte SD.",
    "This won't work: code builds like Project+ have to be on the SD card, not the USB drive. Move {1} to the same spot on your SD card and try again. Your games can stay on USB.":
        "Impossible : les mods de type Project+ doivent figurer sur la carte SD. Déplacez {1} au même emplacement sur la SD. Vos jeux peuvent rester sur l'USB.",
    "Code builds need the game on USB or disc, not the SD card.":
        "Les builds de code nécessitent le jeu sur USB ou disque, pas sur la SD.",
    "{1} is on the USB drive: the game has to be there too.":
        "{1} est sur le stockage USB : le jeu doit s'y trouver également.",
    "This won't work: {1} is on the USB drive, and RiftWii reads the USB drive during a game only when the game is on it too. Put the game on the USB drive, or put {1} in the riftwii folder on your SD card.":
        "Échec : {1} est sur le stockage USB. RiftWii n'accède au stockage USB en jeu que si le jeu y est aussi stocké. Mettez le jeu sur l'USB, ou placez {1} dans le dossier riftwii de votre SD.",
    "Updating RiftWii":
        "Mise à jour de RiftWii",
    "RiftWii {1} is out. Downloading and installing it now; this takes a minute...":
        "RiftWii {1} est disponible. Téléchargement et installation en cours...",
    "Update failed":
        "Échec de la mise à jour",
    "RiftWii {1} could not be installed: {2}. This version keeps working; the new one is at {3}":
        "RiftWii {1} n'a pas pu être installé : {2}. La version actuelle reste fonctionnelle; la nouvelle est à {3}",
    "OK":
        "OK",
    "RiftWii updated":
        "RiftWii mis à jour",
    "RiftWii {1} is installed ({2}). It runs the next time RiftWii starts. Leave to the Homebrew Channel now and start it again?":
        "RiftWii {1} est installé ({2}). Il sera actif au prochain démarrage. Revenir à la Chaîne Homebrew dès maintenant ?",
    "Leave":
        "Quitter",
    "Later":
        "Plus tard",
    "RiftWii {1} is installed. Start RiftWii again to use it.":
        "RiftWii {1} est installé. Relancez RiftWii pour l'utiliser.",
    "Update available":
        "Mise à jour disponible",
    "RiftWii {1} is out (this is {2}). Update now? It takes a minute.":
        "RiftWii {1} est disponible (version actuelle : {2}). Mettre à jour maintenant ?",
    "Not now":
        "Pas maintenant",
    "Are you sure?":
        "Êtes-vous sûr ?",
    "Are you sure you don't want to update? If you had an issue, it could have been fixed in the latest update!":
        "Êtes-vous sûr de ne pas vouloir mettre à jour ? Un bug rencontré a peut-être été corrigé !",
    "RiftWii {1} is out. Settings > Check for a new version installs it.":
        "RiftWii {1} est disponible. Rendez-vous dans Paramètres > Vérifier les mises à jour.",
    "Updates":
        "Mises à jour",
    "Beta":
        "Bêta",
    "Stable":
        "Stable",
    "Beta: every new version, including test builds that may have new bugs. For testers.":
        "Bêta : inclut les versions de test pouvant contenir des bugs. Pour les testeurs.",
    "Stable: only versions marked stable, which testers have checked.":
        "Stable : uniquement les versions validées et certifiées stables.",
    "No stable version is out yet. This is RiftWii {1}.":
        "Aucune version stable disponible pour le moment. Version actuelle : RiftWii {1}.",
    "SD card: {1}":
        "Carte SD : {1}",
    "USB drive: {1}":
        "Stockage USB : {1}",
    "Drive problem":
        "Problème de stockage",
    "Games on that drive are not listed. Check the drive on a computer; details are in sd:/riftwii/session.log.":
        "Les jeux de ce stockage ne sont pas listés. Vérifiez le stockage sur PC; détails dans sd:/riftwii/session.log.",
    "SD card problems":
        "Problème de carte SD",
    "The SD card had trouble while the last game was saving. The details are in sd:/riftwii/cardlog.txt; please send that file to the RiftWii developers.":
        "La carte SD a rencontré une erreur durant la dernière sauvegarde. Consultez sd:/riftwii/cardlog.txt et transmettez-le aux développeurs.",
    "Before you play":
        "Avant de jouer",
    "Press Start again to play.":
        "Appuyez à nouveau sur Démarrer pour jouer.",
    "Game cIOS":
        "cIOS du jeu",
    "Add RiftWii to the Wii Menu?":
        "Ajouter RiftWii au menu Wii ?",
    "RiftWii can have a channel on the Wii Menu, so it starts without the Homebrew Channel. The channel only starts RiftWii from your SD card: RiftWii's updates keep working and the channel never needs reinstalling. The channel installer opens, then brings you back here. Settings can open it again later.":
        "RiftWii peut créer une chaîne sur le menu Wii pour se lancer directement. La chaîne lance la version sur carte SD : les mises à jour fonctionnent sans réinstallation. L'installeur va s'ouvrir puis revenir ici.",
    "Open installer":
        "Ouvrir l'installeur",
    "No thanks":
        "Non merci",
    "RiftWii channel on the Wii Menu":
        "Chaîne RiftWii sur le menu Wii",
    "Installed":
        "Installée",
    "Add":
        "Ajouter",
    "Open the channel installer?":
        "Ouvrir l'installeur de chaîne ?",
    "RiftWii closes and the channel installer opens. It adds, updates or removes the RiftWii channel, then brings you back here.":
        "RiftWii va se fermer pour ouvrir l'installeur. Il permet d'ajouter, mettre à jour ou supprimer la chaîne.",
    "Open":
        "Ouvrir",
    "Cancel":
        "Annuler",
    "The RiftWii channel":
        "La chaîne RiftWii",
    "Opening the installer for":
        "Ouverture de l'installeur pour",
    "RiftWii starts again when it is done.":
        "RiftWii redémarrera une fois l'opération terminée.",
    "A Wii Menu channel that starts RiftWii from the SD card. It holds no copy of RiftWii, so updates keep working. Opens the channel installer, to add, update or remove it.":
        "Une chaîne du menu Wii qui lance RiftWii depuis la carte SD. Ne contient aucun fichier propre, préservant la gestion des mises à jour. Ouvre l'installeur de chaîne.",
    "Copy apps/riftwii_channel from the RiftWii zip to the SD card first":
        "Copiez d'abord apps/riftwii_channel depuis le zip de RiftWii vers la carte SD",
    "Dolphin checks real signatures, so the channel can only be installed on a Wii":
        "Dolphin vérifiant les signatures officielles, cette chaîne s'installe uniquement sur une vraie Wii",
    "Installing the channel needs a d2x cIOS in slot 249, 250 or 251":
        "L'installation nécessite un cIOS d2x dans les slots 249, 250 ou 251",
    "(Log: sd:/riftwii/session.log)":
        "(Journal : sd:/riftwii/session.log)",
    "RiftWii needs an SD card":
        "RiftWii nécessite une carte SD",
    "USB mode is not supported yet":
        "Le mode 100% USB n'est pas encore pris en charge",
    "No SD card was found":
        "Aucune carte SD trouvée",
    "RiftWii was started from a USB drive. It keeps its settings, logs and saves on the SD card, so for now it needs one to run. Copy the sd-card folder from the RiftWii zip to a FAT32 SD card, put the card in the Wii and start RiftWii from it.":
        "RiftWii a été démarré depuis un stockage USB. Il nécessite une carte SD pour ses paramètres et sauvegardes. Copiez le dossier sd-card du fichier zip sur une carte SD au format FAT32 puis insérez-la.",
    "RiftWii keeps its settings, logs and saves on the SD card and could not read one. Put a FAT32 SD card in the Wii with the sd-card folder from the RiftWii zip on it, then start RiftWii again.":
        "Impossible de lire la carte SD contenant vos configurations et sauvegardes. Veuillez insérer une carte SD FAT32 munie du dossier sd-card extrait de l'archive zip.",
    "Need a card? Scan this.":
        "Besoin d'une carte ? Scannez ceci.",
    "Is this a burned disc?":
        "S'agit-il d'un disque gravé ?",
    "The drive could not read this disc. If it is a burned disc, RiftWii can read it through d2x on older Wiis (later Wii drives read only Nintendo discs). Burned discs can wear out the disc drive sooner: use them at your own risk. The menu then restarts under IOS{1} for this session.":
        "Le lecteur n'a pas pu lire ce disque. S'il est gravé, RiftWii peut le lire via d2x sur les anciennes Wii (les modèles récents lisent uniquement les disques originaux). Les disques gravés usent la lentille plus rapidement. Le menu redémarrera sous l'IOS{1}.",
    "Burned disc: it wears the Wii's disc drive more than a pressed disc does. Play at your own risk.":
        "Disque gravé : usure prématurée de la lentille par rapport à un disque officiel. À utiliser à vos risques.",
    "Try with d2x":
        "Essayer avec d2x",
    "The menu runs under IOS{1} for this session, to read burned discs. Pick the disc.":
        "Le menu s'exécute sous l'IOS{1} pour cette session afin de lire les disques gravés. Sélectionnez le disque.",
    "The drive cannot read this disc, even through d2x. Later Wii drives read only Nintendo discs, never burned ones; on an older Wii, the burn may be bad.":
        "Lecture impossible même via d2x. Les lecteurs Wii récents refusent les disques gravés; sur une ancienne Wii, la gravure est peut-être défectueuse.",
    "The drive cannot read this disc. If it is a burned disc, RiftWii needs a d2x cIOS to read it.":
        "Le lecteur ne peut pas lire ce disque. Si c'est un disque gravé, un cIOS d2x est indispensable.",
    "RiftWii could not restart. Start it again from the Homebrew Channel.":
        "Redémarrage de RiftWii impossible. Relancez-le depuis la Chaîne Homebrew.",
    "1 game":
        "1 jeu",
    "{1} games":
        "{1} jeux",
    "Search \"{1}\"":
        "Rechercher \"{1}\"",
    "No game matches \"{1}\". Press 1 for all games.":
        "Aucun jeu ne correspond à \"{1}\". Appuyez sur 1 pour tous les afficher.",
    "1: all games":
        "1 : tous les jeux",
    "Search games":
        "Rechercher un jeu",
    "Search":
        "Rechercher",
    "Space":
        "Espace",
    "Clear":
        "Effacer",
    "Del":
        "Suppr",
    "Homebrew Channel":
        "Chaîne Homebrew",
    "Wii Menu":
        "Menu Wii",
    "Power off":
        "Éteindre",
    "HOME Menu":
        "Menu HOME",
    "Close":
        "Fermer",
    "Opens the HOME Menu, as HOME does: the Homebrew Channel, the Wii Menu, Priiloader or power off.":
        "Ouvre le menu HOME : Chaîne Homebrew, Menu Wii, Priiloader ou extinction.",
    "Sending a report":
        "Envoi du rapport",
    "Gathering the logs and sending them. This can take half a minute...":
        "Collecte et envoi des journaux d'erreurs en cours (environ 30 secondes)...",
    "Report not sent":
        "Rapport non envoyé",
    "It could not be sent: {1}. It is saved on the SD card as sd:/riftwii/report.txt: send that file instead.":
        "Échec de l'envoi : {1}. Le fichier est sauvegardé sous sd:/riftwii/report.txt pour transmission manuelle.",
    "It could not be sent or saved: {1}":
        "Impossible d'envoyer ou de sauvegarder le rapport : {1}",
    "Send this link to whoever is helping you, or scan the code with a phone:":
        "Transmettez ce lien à votre assistant ou scannez ce QR code :",
    "The report was too big, so only its start was kept.":
        "Rapport trop volumineux : seul le début a été conservé.",
    "Report sent":
        "Rapport envoyé",
    "It holds RiftWii's logs and settings, the game's choices and packs, and which console, IOS and controllers this is. It goes to paste.rs, or dpaste.com when paste.rs can't be reached, where anyone with its link can read it.":
        "Contient la configuration, l'IOS, les manettes et les journaux de RiftWii. Hébergé publiquement sur paste.rs ou dpaste.com pour être partagé via le lien.",
    "RiftWii crashed last time":
        "RiftWii a planté la dernière fois",
    "The game crashed last time":
        "Le jeu a planté la dernière fois",
    "The last launch failed":
        "Le dernier lancement a échoué",
    "Send a report of what happened?":
        "Envoyer un rapport d'incident ?",
    "Send":
        "Envoyer",
    "Send a problem report":
        "Envoyer un rapport de problème",
    "Send a problem report?":
        "Transmettre un rapport de dysfonctionnement ?",
    "Something went wrong? Sends what it takes to find out to a paste site, and shows a link to pass on.":
        "Un problème ? Envoie les informations nécessaires sur un serveur d'analyse et vous fournit un lien de partage.",
    "WARNING: this can freeze your Wii U":
        "ATTENTION : risque de blocage de votre Wii U",
    "On a Wii U, the GameCube adapter in games can freeze the console at 97% while a game from the SD card or a USB drive starts. It works some times and freezes others, and RiftWii cannot tell beforehand. If it freezes, hold the power button to turn the console off. Automatic is safe: it leaves the adapter out of those games.":
        "Sur Wii U, l'adaptateur GameCube peut figer le chargement à 97% lors du lancement d'un jeu SD/USB. En cas de blocage, maintenez le bouton Power enfoncé. Le mode \"Automatique\" évite ce risque.",
    "Turn it on anyway":
        "Activer malgré tout",
    "Some game launches WILL freeze and need the power button. Only turn this on to test the adapter, and send a problem report when it freezes. You can set it back to Automatic here at any time.":
        "Certains jeux vont FIGER la console et nécessiter une extinction forcée. N'activez cette option que pour effectuer des tests.",
    "Yes, turn it on":
        "Oui, activer",
    "Keep it as it is":
        "Conserver l'état actuel",
    "WARNING: on this Wii U, games from the SD card or a USB drive can freeze at 97% with this on. Use Automatic unless you are testing the adapter.":
        "ATTENTION : sur cette Wii U, le chargement peut bloquer à 97%. Privilégiez le mode Automatique.",
    "Left as it was.":
        "Aucune modification effectuée.",
    "Theme":
        "Thème",
    "Default":
        "Par défaut",
    "The menu's colours and pictures. Themes are folders in sd:/riftwii/themes (docs/THEMES.md on GitHub).":
        "Apparence visuelle du menu. Les thèmes sont stockés dans sd:/riftwii/themes.",
    "No themes in sd:/riftwii/themes. The zip's themes folder has one to copy there.":
        "Aucun thème trouvé dans sd:/riftwii/themes. L'archive zip d'origine en propose à installer.",
    "Restart the menu?":
        "Redémarrer le menu ?",
    "RiftWii's menu restarts to show {1}. Your games and settings stay as they are.":
        "Le menu RiftWii redémarre pour appliquer {1}. Vos jeux et paramètres sont conservés.",
    "Restart":
        "Redémarrer",
    "Theme: {1}":
        "Thème : {1}",
    "Clock":
        "Horloge",
    "12-hour":
        "12 heures",
    "24-hour":
        "24 heures",
    "How Home's clock writes the time: 12-hour (with AM and PM) or 24-hour. Automatic writes it as the menu's language does.":
        "Comment l'horloge de l'accueil affiche l'heure : 12 heures (avec AM et PM) ou 24 heures. Automatique l'affiche comme la langue du menu.",
    "Menu font":
        "Police du menu",
    "Add a picture:":
        "Ajouter une image :",
    "The letters the menu is written in: RiftWii's own, or the Wii Menu's, read from this Wii.":
        "Style typographique : police personnalisée de RiftWii ou police officielle extraite du menu Wii.",
    "the Wii Menu's font":
        "police du menu Wii",
    "RiftWii's font":
        "police de RiftWii",
    "Menu font: {1}":
        "Police du menu : {1}",
    "The Wii Menu's font could not be read, so RiftWii's is used.":
        "Police originale du menu Wii introuvable, utilisation de la police native de RiftWii.",
    "Files from a Mac on the SD card":
        "Fichiers cachés macOS sur la carte SD",
    "This SD card has hidden files that macOS makes when it copies (names that start with \"._\"). RiftWii skips them, but they fill the card and can confuse other homebrew. To remove them, put the card in your Mac, open Terminal and type: dot_clean -m /Volumes/ followed by the card's name. On Windows, delete the files whose names start with \"._\".":
        "Cette carte contient des fichiers système macOS commençant par \"._\". RiftWii les ignore mais ils prennent de la place. Pour les supprimer sur Mac, utilisez la commande Terminal `dot_clean -m /Volumes/[NomCarte]`. Sur Windows, supprimez manuellement les fichiers \"._\".",
    "This code mod can't start":
        "Ce mod de code ne peut pas démarrer",
    "{1} has more codes than the game has room for ({2}, room for {3}). It needs the file gameconfig.txt (or gc.txt) from the same download as the mod. Copy it into the mod's own folder on the SD card, {4}, so it can't replace another mod's. Not in the download? Ask whoever made the mod.":
        "{1} contient plus de codes que la mémoire disponible ({2}, limite à {3}). Le fichier gameconfig.txt (ou gc.txt) inclus avec le mod est requis dans {4}.",
    "The codes take {1}, but {2} only makes room for {3}. Turn off some cheats on this game's Cheats page, or turn off a code mod.":
        "Les codes requièrent {1}, mais {2} n'alloue que {3}. Désactivez certains codes de triche ou mods de code pour libérer de la mémoire.",
    "Aspect ratio":
        "Rapport d'aspect",
    "Rumble":
        "Vibrations",
    "Wii Remote speaker":
        "Haut-parleur de la Wiimote",
    "Region strings fix":
        "Correction des chaînes régionales",
    "Makes the game use 4:3 or widescreen 16:9 whatever the Wii's TV setting says. Not every game can be changed.":
        "Force l'affichage en 4:3 ou 16:9 indépendamment de la configuration système. Non compatible avec tous les jeux.",
    "Off: the Wii Remotes never rumble in this game.":
        "Désactivé : désactive totalement les vibrations de la Wiimote sur ce jeu.",
    "Off: no sound from the Wii Remotes' speakers in this game.":
        "Désactivé : coupe le haut-parleur intégré des Wiimotes pour ce jeu.",
    "For a game from another region (an import): the game sees its own region's country names where it looks for the console's.":
        "Pour les jeux importés d'une autre région : transmet au jeu les identifiants de pays correspondant à sa zone d'origine.",
    "Don't show again":
        "Ne plus afficher",
}

# Languages kept in tables of their own: written after LANGS, every msgid
# of T with its text here (an empty msgstr, English on screen, when none).
MORE = {"ko": KO, "fr": FR}
NAMES["ko"] = "Korean"
NAMES["fr"] = "French"


def escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def check(repo):
    """Every msgid must appear as a literal in the menu's sources, or it
    would never be looked up."""
    sources = ["wii/rift_menu.cpp", "wii/gameextras.cpp", "wii/gui_gamegrid.cpp", "wii/channel.cpp", "wii/frontend.cpp",
               "wii/codebuilds.cpp", "wii/gui_searchkeys.cpp", "wii/main.cpp", "wii/vsdmake.cpp"]
    text = "".join(open(os.path.join(repo, p), encoding="utf-8").read() for p in sources)
    missing = [k for k in T if '"%s"' % escape(k) not in text]
    for k in missing:
        print("not in the sources:", k)
    return not missing


def main():
    repo = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    if not check(repo):
        raise SystemExit(1)
    root = os.path.join(repo, "wii", "lang")
    for i, lang in enumerate(LANGS):
        lines = [
            "# RiftWii menu: %s. Written by tools/lang_source.py; edit the table there." % NAMES[lang],
            "# A copy at sd:/riftwii/lang/%s.po overrides these entries on the Wii." % lang,
            'msgid ""',
            'msgstr "Content-Type: text/plain; charset=UTF-8\\n"',
            "",
        ]
        for msgid, row in T.items():
            lines.append('msgid "%s"' % escape(msgid))
            lines.append('msgstr "%s"' % escape(row[i]))
            lines.append("")
        with open(os.path.join(root, lang + ".po"), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines))
    for lang, table in MORE.items():
        unknown = [k for k in table if k not in T]
        for k in unknown:
            print("%s: not a msgid of the table:" % lang, k)
        if unknown:
            raise SystemExit(1)
        lines = [
            "# RiftWii menu: %s. Written by tools/lang_source.py; edit the table there." % NAMES[lang],
            "# A copy at sd:/riftwii/lang/%s.po overrides these entries on the Wii." % lang,
            'msgid ""',
            'msgstr "Content-Type: text/plain; charset=UTF-8\\n"',
            "",
        ]
        for msgid in T:
            lines.append('msgid "%s"' % escape(msgid))
            lines.append('msgstr "%s"' % escape(table.get(msgid, "")))
            lines.append("")
        with open(os.path.join(root, lang + ".po"), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines))
        print("%s: %d of %d translated" % (lang, sum(1 for k in T if table.get(k)), len(T)))
    print("%d entries, %d languages" % (len(T), len(LANGS) + len(MORE)))


if __name__ == "__main__":
    main()
