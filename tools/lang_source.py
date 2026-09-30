#!/usr/bin/env python3
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
    "{1} on": ("{1} activados", "{1}個オン", "{1} ativados", "{1} attivi"),
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
    "Remove stretches the picture to fill the screen.": ("Quitar estira la imagen para llenar la pantalla.",
                                                         "「なくす」にすると画面いっぱいに引き伸ばします。",
                                                         "Remover estica a imagem para preencher a tela.",
                                                         "Rimuovi allarga l'immagine per riempire lo schermo."),
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
    "This cheat has values to fill in (the X's). Edit the file first.": (
        "Este truco tiene valores por rellenar (las X). Edita el archivo antes.",
        "このチートには入力する値(X の部分)があります。先にファイルを編集してください。",
        "Esta trapaça tem valores a preencher (os X). Edite o arquivo antes.",
        "Questo trucco ha dei valori da inserire (le X). Modifica prima il file."),
    "Cheats are only applied when this is On.": ("Los trucos solo se aplican si esto está en Sí.",
                                                 "これがオンのときだけチートが使われます。",
                                                 "As trapaças só são aplicadas quando isto está em Sim.",
                                                 "I trucchi si applicano solo quando questo è su Sì."),
    "Replaces the file with the latest cheats from the GeckoCodes archive.": (
        "Reemplaza el archivo con los últimos trucos del archivo de GeckoCodes.",
        "GeckoCodesアーカイブの最新のチートでファイルを置き換えます。",
        "Substitui o arquivo pelas trapaças mais recentes do acervo GeckoCodes.",
        "Sostituisce il file con i trucchi più recenti dell'archivio GeckoCodes."),
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
    "Credits and licence": ("Créditos y licencia", "クレジットとライセンス", "Créditos e licença", "Riconoscimenti e licenza"),
    "View": ("Ver", "表示", "Ver", "Vedi"),
    "Who RiftWii's parts come from, its licence (the GNU GPL, version 3 or later) and where its source is.": (
        "De quién vienen las partes de RiftWii, su licencia (la GNU GPL, versión 3 o posterior) y dónde está su código fuente.",
        "RiftWiiの各部分の作者、ライセンス (GNU GPL バージョン3以降) とソースコードの場所。",
        "De quem vêm as partes do RiftWii, sua licença (a GNU GPL, versão 3 ou posterior) e onde está seu código-fonte.",
        "Da chi provengono le parti di RiftWii, la sua licenza (la GNU GPL, versione 3 o successiva) e dove si trova il suo codice sorgente."),
    "Experimental. When the adapter is plugged in as a game starts, its controllers fill the ports that have none plugged in, in games that support the GameCube controller. It needs IOS 58 or a d2x cIOS.": (
        "Experimental. Si el adaptador está conectado al iniciar un juego, sus mandos ocupan los puertos que no tienen ninguno conectado, en los juegos compatibles con el mando de GameCube. Necesita IOS 58 o un cIOS d2x.",
        "試験的な機能です。ゲーム開始時に接続タップがつながっていれば、ゲームキューブコントローラに対応したゲームで、何もつながっていないポートに接続タップのコントローラが入ります。IOS 58かd2x cIOSが必要です。",
        "Experimental. Se o adaptador estiver conectado quando um jogo começa, os controles dele ocupam as portas sem nenhum conectado, nos jogos compatíveis com o controle de GameCube. Precisa do IOS 58 ou de um cIOS d2x.",
        "Sperimentale. Se l'adattatore è collegato quando parte un gioco, i suoi controller occupano le porte senza nulla collegato, nei giochi che supportano il controller GameCube. Serve l'IOS 58 o un cIOS d2x."),
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
    "Covers shows each game's box art from GameTDB, fetched while Home is open when downloads are on. Names shows the names only.": (
        "Portadas muestra la carátula de cada juego de GameTDB, que se descarga con el inicio abierto si las descargas están activadas. Nombres muestra solo los nombres.",
        "パッケージではGameTDBの各ゲームのパッケージ画像を表示します。ダウンロードがオンなら、ホームを開いている間に取得します。名前では名前だけを表示します。",
        "Capas mostra a capa de cada jogo do GameTDB, baixada com o início aberto quando os downloads estão ligados. Nomes mostra só os nomes.",
        "Copertine mostra la copertina di ogni gioco da GameTDB, scaricata con la home aperta se i download sono attivi. Nomi mostra solo i nomi."),
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
    "Experimental. In a game, hold 1 and press HOME (or hold L and R and press Down on a GameCube controller). The pictures go to sd:/riftwii/screenshots the next time RiftWii starts. Some games and mods may not work with it.": (
        "Experimental. En un juego, mantén 1 y pulsa HOME (o mantén L y R y pulsa Abajo en un mando de GameCube). Las imágenes van a sd:/riftwii/screenshots la próxima vez que se inicie RiftWii. Puede que algunos juegos y mods no funcionen con esto.",
        "試験的な機能です。ゲーム中に1を押したままHOMEを押します(ゲームキューブコントローラーならLとRを押したまま下)。画像は次にRiftWiiを起動したときにsd:/riftwii/screenshotsに保存されます。一部のゲームやMODでは使えないことがあります。",
        "Experimental. Em um jogo, segure 1 e aperte HOME (ou segure L e R e aperte Baixo num controle de GameCube). As imagens vão para sd:/riftwii/screenshots na próxima vez que o RiftWii abrir. Alguns jogos e mods podem não funcionar com isso.",
        "Sperimentale. In un gioco, tieni premuto 1 e premi HOME (o tieni premuti L e R e premi Giù su un controller GameCube). Le immagini vanno in sd:/riftwii/screenshots al prossimo avvio di RiftWii. Alcuni giochi e mod potrebbero non funzionare."),
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
    "Code builds need the game on USB or disc, not the SD card.": (
        "Las builds de códigos necesitan el juego en USB o en disco, no en la SD.",
        "コードビルドはSDカードではなく、USBかディスクのゲームで遊んでください。",
        "Builds de códigos precisam do jogo no USB ou no disco, não no SD.",
        "Le build di codici vogliono il gioco su USB o su disco, non sulla SD."),
    "This won't work: code builds like Project+ read the SD card while you play, so the game can't be on the SD card too. Put it on a USB drive or use the disc.": (
        "Así no va a funcionar: las builds de códigos como Project+ leen la SD mientras juegas, así que el juego no puede estar también en la SD. Ponlo en un USB o usa el disco.",
        "このままでは動きません。Project+などのコードビルドはプレイ中にSDカードを読むので、ゲームをSDカードに置くことはできません。USBドライブに入れるか、ディスクを使ってください。",
        "Assim não vai funcionar: builds de códigos como o Project+ leem o SD enquanto você joga, então o jogo não pode estar no SD também. Coloque-o num USB ou use o disco.",
        "Così non funziona: le build di codici come Project+ leggono la SD mentre giochi, quindi il gioco non può stare anche sulla SD. Mettilo su un'unità USB o usa il disco."),
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
    "Gathering the logs and sending them to paste.rs. This can take half a minute...": (
        "Reuniendo los registros y enviándolos a paste.rs. Puede tardar medio minuto...",
        "ログを集めてpaste.rsに送信しています。30秒ほどかかることがあります...",
        "Juntando os registros e enviando para o paste.rs. Pode levar meio minuto...",
        "Raccolta dei log e invio a paste.rs. Può richiedere mezzo minuto..."),
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
    "It holds RiftWii's logs and settings, the game's choices and packs, and which console, IOS and controllers this is. It goes to paste.rs, where anyone with its link can read it.": (
        "Incluye los registros y ajustes de RiftWii, las opciones y packs del juego, y qué consola, IOS y mandos son. Se envía a paste.rs, donde cualquiera con el enlace puede leerlo.",
        "RiftWiiのログと設定、ゲームの選択とパック、本体・IOS・コントローラーの情報が入っています。paste.rsに送られ、リンクを知っている人なら誰でも読めます。",
        "Ele traz os registros e as configurações do RiftWii, as escolhas e os packs do jogo, e qual console, IOS e controles são. Vai para o paste.rs, onde qualquer pessoa com o link pode lê-lo.",
        "Contiene i log e le impostazioni di RiftWii, le scelte e i pack del gioco, e quali console, IOS e controller sono. Va su paste.rs, dove chiunque abbia il link può leggerla."),
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
    "Something went wrong? Sends what it takes to find out to paste.rs, and shows a link to pass on.": (
        "¿Algo salió mal? Envía a paste.rs lo necesario para averiguarlo y muestra un enlace para compartir.",
        "問題が起きましたか？原因を調べるのに必要な情報をpaste.rsに送り、共有用のリンクを表示します。",
        "Algo deu errado? Envia ao paste.rs o necessário para descobrir e mostra um link para compartilhar.",
        "Qualcosa non va? Invia a paste.rs quello che serve per capirlo e mostra un link da condividere."),
}


def escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def check(repo):
    """Every msgid must appear as a literal in the menu's sources, or it
    would never be looked up."""
    sources = ["wii/rift_menu.cpp", "wii/gameextras.cpp", "wii/gui_gamegrid.cpp", "wii/channel.cpp", "wii/frontend.cpp"]
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
    print("%d entries, %d languages" % (len(T), len(LANGS)))


if __name__ == "__main__":
    main()
