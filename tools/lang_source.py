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
    "1: filter   2: settings   +: rescan": ("1: filtro   2: ajustes   +: buscar de nuevo",
                                            "1: 絞り込み   2: 設定   +: 再検索",
                                            "1: filtro   2: configurações   +: procurar de novo",
                                            "1: filtro   2: impostazioni   +: cerca di nuovo"),
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
    "{1} mod pack(s) for this game. Press A to turn them on.": (
        "{1} paquete(s) de mods para este juego. Pulsa A para activarlos.",
        "このゲームのMODパックが{1}個あります。Aでオンにできます。",
        "{1} pacote(s) de mods para este jogo. Aperte A para ativá-los.",
        "{1} pacchetto/i di mod per questo gioco. Premi A per attivarli."),
    "No mods on the SD card": ("No hay mods en la tarjeta SD", "SDカードにMODがありません", "Nenhum mod no cartão SD",
                               "Nessuna mod sulla scheda SD"),
    "No mods for this game": ("No hay mods para este juego", "このゲームのMODはありません", "Nenhum mod para este jogo",
                              "Nessuna mod per questo gioco"),
    "Put Riivolution XML in sd:/riivolution": ("Pon los XML de Riivolution en sd:/riivolution",
                                               "RiivolutionのXMLを sd:/riivolution に入れてください",
                                               "Coloque os XML do Riivolution em sd:/riivolution",
                                               "Metti gli XML di Riivolution in sd:/riivolution"),
    "{1} XML file(s) are for other games": ("{1} archivo(s) XML son de otros juegos", "{1}個のXMLは他のゲーム用です",
                                            "{1} arquivo(s) XML são de outros jogos", "{1} file XML sono per altri giochi"),
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
    "No cheat file. Put one at {1}": ("No hay archivo de trucos. Pon uno en {1}", "チートファイルがありません。{1} に置いてください",
                                      "Nenhum arquivo de trapaças. Coloque um em {1}", "Nessun file di trucchi. Mettine uno in {1}"),
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
    "In games that support the GameCube controller, the adapter's controllers fill the ports that have none plugged in. It needs IOS 58 or a d2x cIOS.": (
        "En los juegos compatibles con el mando de GameCube, los mandos del adaptador ocupan los puertos que no tienen ninguno conectado. Necesita IOS 58 o un cIOS d2x.",
        "ゲームキューブコントローラに対応したゲームで、何もつながっていないポートに接続タップのコントローラが入ります。IOS 58かd2x cIOSが必要です。",
        "Nos jogos compatíveis com o controle de GameCube, os controles do adaptador ocupam as portas sem nenhum conectado. Precisa do IOS 58 ou de um cIOS d2x.",
        "Nei giochi che supportano il controller GameCube, i controller dell'adattatore occupano le porte senza nulla collegato. Serve l'IOS 58 o un cIOS d2x."),
    "The adapter is left alone.": ("El adaptador no se usa.", "接続タップは使いません。", "O adaptador não é usado.",
                                   "L'adattatore non viene usato."),
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
    "Back to the Homebrew Channel.": (
        "Vuelve al Homebrew Channel.",
        "Homebrew Channelに戻ります。",
        "Volta ao Homebrew Channel.",
        "Torna all'Homebrew Channel."),
    "No d2x cIOS was found in slots 248 to 252, so the menu runs under IOS 58. Install d2x to play games from SD or USB.": (
        "No se encontró ningún cIOS d2x en los slots 248 a 252, así que el menú usa el IOS 58. Instala d2x para jugar desde SD o USB.",
        "スロット248〜252にd2x cIOSがないため、メニューはIOS 58で動いています。SDやUSBのゲームを遊ぶにはd2xを入れてください。",
        "Nenhum cIOS d2x foi encontrado nos slots 248 a 252, então o menu usa o IOS 58. Instale o d2x para jogar pelo SD ou USB.",
        "Nessun cIOS d2x trovato negli slot da 248 a 252, quindi il menu usa l'IOS 58. Installa d2x per giocare da SD o USB."),
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
}


def escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def check(repo):
    """Every msgid must appear as a literal in the menu's sources, or it
    would never be looked up."""
    sources = ["wii/rift_menu.cpp", "wii/gameextras.cpp", "wii/gui_gamegrid.cpp"]
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
