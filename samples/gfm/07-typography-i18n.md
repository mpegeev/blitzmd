# Typography and Internationalization

This file tests text shaping, font fallback, line breaking and bidirectional layout. It contains long paragraphs in English, Russian, Ukrainian, German, French, Greek, Chinese, Japanese, Korean, Arabic, Hebrew, Hindi, Thai, Georgian and Armenian, plus a font-fallback table of rarer scripts. It also covers mixed left-to-right and right-to-left text, emoji, mathematical and typographic symbols, combining diacritics (including "Zalgo" text), invisible characters (zero-width, bidi marks, soft hyphens, special spaces), words far longer than the window, and headings of every level in mixed scripts. No character here should render as an empty box (tofu). If one does, font fallback is missing for that script.

## Contents

- [English](#english)
- [European languages](#european-languages)
- [Greek](#greek)
- [CJK](#cjk)
- [Right-to-left scripts](#right-to-left-scripts)
- [Indic, Southeast Asian and Caucasian scripts](#indic-southeast-asian-and-caucasian-scripts)
- [Universal Declaration of Human Rights, Article 1](#universal-declaration-of-human-rights-article-1)
- [Font fallback: rarer scripts](#font-fallback-rarer-scripts)
- [Emoji](#emoji)
- [Symbols](#symbols)
- [Quotes, dashes and punctuation](#quotes-dashes-and-punctuation)
- [Combining characters](#combining-characters)
- [Invisible and special-width characters](#invisible-and-special-width-characters)
- [Long words and line breaking](#long-words-and-line-breaking)
- [Formatting across scripts](#formatting-across-scripts)
- [Headings in mixed scripts](#headings-in-mixed-scripts)

---

## English

Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.

Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam, eaque ipsa quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt explicabo. **Nemo enim ipsam voluptatem** quia voluptas sit aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos qui ratione voluptatem sequi nesciunt. *Neque porro quisquam est*, qui dolorem ipsum quia dolor sit amet, consectetur, adipisci velit, sed quia non numquam eius modi tempora incidunt ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum `exercitationem` ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? [Quis autem vel eum](https://en.wikipedia.org/wiki/Lorem_ipsum) iure reprehenderit qui in ea voluptate velit esse quam nihil molestiae consequatur, vel illum qui dolorem eum fugiat quo voluptas nulla pariatur?

The quick brown fox jumps over the lazy dog. THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG. 0123456789. Sphinx of black quartz, judge my vow! Pack my box with five dozen liquor jugs. How vexingly quick daft zebras jump!

---

## European languages

### Russian (Русский)

Все счастливые семьи похожи друг на друга, каждая несчастливая семья несчастлива по-своему. Всё смешалось в доме Облонских. Жена узнала, что муж был в связи с бывшею в их доме француженкою-гувернанткой, и объявила мужу, что не может жить с ним в одном доме. Положение это продолжалось уже третий день и мучительно чувствовалось и самими супругами, и всеми членами семьи, и домочадцами. — *Лев Толстой, «Анна Каренина»*

Мой дядя самых честных правил,\
Когда не в шутку занемог,\
Он уважать себя заставил\
И лучше выдумать не мог.\
— **А. С. Пушкин**, «Евгений Онегин»

Буква «ё» и её отсутствие: ёжик/ежик, всё/все. Твёрдый знак: подъезд, объявление. Заглавные: ЁЛКА, ЪЕЗД.

### Ukrainian (Українська)

Як умру, то поховайте\
Мене на могилі\
Серед степу широкого\
На Вкраїні милій,\
Щоб лани широкополі,\
І Дніпро, і кручі\
Було видно, було чути,\
Як реве ревучий.\
— *Тарас Шевченко, «Заповіт»*

Українські літери, яких немає в російській абетці: Ґґ, Єє, Іі, Її, та апостроф: п'ять, м'ята, об'єкт (ASCII-апостроф) і п’ять, м’ята (типографський апостроф U+2019).

### German (Deutsch)

Als Gregor Samsa eines Morgens aus unruhigen Träumen erwachte, fand er sich in seinem Bett zu einem ungeheueren Ungeziefer verwandelt. Er lag auf seinem panzerartig harten Rücken und sah, wenn er den Kopf ein wenig hob, seinen gewölbten, braunen, von bogenförmigen Versteifungen geteilten Bauch, auf dessen Höhe sich die Bettdecke, zum gänzlichen Niedergleiten bereit, kaum noch erhalten konnte. — *Franz Kafka, „Die Verwandlung“*

Umlaute und Eszett: Ärger, Öl, Übermut, äöü ÄÖÜ, Straße, Maßstab, Größe. Großes Eszett: GROẞE STRAẞE (U+1E9E). „Zitat mit ‚verschachteltem‘ Zitat“. Zahlen: 1.234.567,89 €.

### French (Français)

Longtemps, je me suis couché de bonne heure. Parfois, à peine ma bougie éteinte, mes yeux se fermaient si vite que je n’avais pas le temps de me dire : « Je m’endors. » Et, une demi-heure après, la pensée qu’il était temps de chercher le sommeil m’éveillait ; je voulais poser le volume que je croyais avoir encore dans les mains et souffler ma lumière. — *Marcel Proust, « Du côté de chez Swann »*

French typography puts a narrow no-break space (U+202F) before `; : ! ?` and inside guillemets. In this sentence, the spaces inside « » and before ! and ? are those spaces: « Bonjour ! » et « Ça va ? ». Ligatures and accents: cœur, œuvre, Æsop, naïve, Noël, garçon, à côté, où, ÉCOLE, ÇA.

### Other Latin-script languages

- **Polish:** Zażółć gęślą jaźń. Pchnąć w tę łódź jeża lub ośm skrzyń fig.
- **Czech:** Příliš žluťoučký kůň úpěl ďábelské ódy.
- **Turkish:** Pijamalı hasta yağız şoföre çabucak güvendi. (dotless ı and dotted İ: İstanbul, ILIK, ılık)
- **Vietnamese:** Tiếng Việt có dấu: Những đứa trẻ vui vẻ chạy nhảy trên đồng cỏ xanh mướt.
- **Hungarian:** Árvíztűrő tükörfúrógép.
- **Icelandic:** Kæmi ný öxi hér, ykist þjófum nú bæði víl og ádrepa.
- **Romanian:** Înțelepciunea și ștergătorul (comma-below ș ț, not cedilla ş ţ).
- **Esperanto:** Eĥoŝanĝo ĉiuĵaŭde.

---

## Greek

Ἄνδρα μοι ἔννεπε, Μοῦσα, πολύτροπον, ὃς μάλα πολλὰ πλάγχθη, ἐπεὶ Τροίης ἱερὸν πτολίεθρον ἔπερσε· πολλῶν δ’ ἀνθρώπων ἴδεν ἄστεα καὶ νόον ἔγνω, πολλὰ δ’ ὅ γ’ ἐν πόντῳ πάθεν ἄλγεα ὃν κατὰ θυμόν. — *Ὅμηρος, Ὀδύσσεια* (polytonic Greek, with breathings, accents and iota subscripts)

Ξεσκεπάζω την ψυχοφθόρα βδελυγμία. Τάχιστη αλώπηξ βαφής ψημένη γη, δρασκελίζει υπέρ νωθρού κυνός. (monotonic modern Greek; final sigma ς vs σ)

---

## CJK

### Chinese (中文)

道可道，非常道；名可名，非常名。无名天地之始，有名万物之母。故常无欲，以观其妙；常有欲，以观其徼。此两者同出而异名，同谓之玄。玄之又玄，众妙之门。——《道德经》（简体）

子曰：「學而時習之，不亦說乎？有朋自遠方來，不亦樂乎？人不知而不慍，不亦君子乎？」——《論語》（繁體）

Chinese text has no spaces, so a line may break between almost any two characters. Punctuation such as ，。；： must never start a new line. Mixed with English: 我们使用 Direct2D 和 DirectWrite 来渲染 Markdown 文档，支持 GFM 表格、任务列表和脚注。版本号 v2.3.1 于 2026 年 9 月 23 日发布。

### Japanese (日本語)

吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。何でも薄暗いじめじめした所でニャーニャー泣いていた事だけは記憶している。吾輩はここで始めて人間というものを見た。しかもあとで聞くとそれは書生という人間中で一番獰悪な種族であったそうだ。——夏目漱石『吾輩は猫である』

ひらがな、カタカナ、漢字、全角英数字（ＡＢＣ１２３）、半角カナ（ｱｲｳｴｵ）が混在する文章。句読点「、」「。」や閉じ括弧「」」は行頭に来てはいけません（禁則処理）。長音「ー」と小書き文字「っ」「ゃ」もテストします。

### Korean (한국어)

모든 인간은 태어날 때부터 자유로우며 그 존엄과 권리에 있어 동등하다. 인간은 천부적으로 이성과 양심을 부여받았으며 서로 형제애의 정신으로 행동하여야 한다. 한국어는 단어 사이에 띄어쓰기를 하므로 영어처럼 공백에서 줄을 바꿀 수 있습니다. 다람쥐 헌 쳇바퀴에 타고파.

---

## Right-to-left scripts

On github.com each rendered block gets `dir="auto"`, so a paragraph whose first strong character is Arabic or Hebrew is laid out right-to-left and aligned to the right. Punctuation at the end of an RTL sentence must appear at the **left** end.

### Arabic (العربية)

يولد جميع الناس أحرارًا متساوين في الكرامة والحقوق. وقد وهبوا عقلاً وضميرًا وعليهم أن يعامل بعضهم بعضًا بروح الإخاء. اللغة العربية تكتب من اليمين إلى اليسار، وحروفها تتصل ببعضها وتتغير أشكالها حسب موقعها في الكلمة: في البداية والوسط والنهاية.

صدر الإصدار 2.3.1 من البرنامج في 23 سبتمبر 2026، ويدعم ملفات Markdown بترميز UTF-8 وUTF-16. الأرقام العربية الهندية: ٠١٢٣٤٥٦٧٨٩. التشكيل: بِسْمِ اللَّهِ الرَّحْمَٰنِ الرَّحِيمِ.

### Hebrew (עברית)

כל בני האדם נולדו בני חורין ושווים בערכם ובזכויותיהם. כולם חוננו בתבונה ובמצפון, לפיכך חובה עליהם לנהוג איש ברעהו ברוח של אחוה. הגרסה החדשה של Total Commander 11.51 תומכת בתוספים מסוג WLX, כולל תצוגת Markdown.

### Mixed direction in one paragraph

An English paragraph with embedded RTL words: the Hebrew word שלום means "peace", and so does the Arabic word سلام. Numbers stay left-to-right inside RTL runs: גרסה 2.0 (בטא) and الإصدار 3.14 (تجريبي). Parentheses must be **mirrored** in RTL runs.

פסקה בעברית עם מילים באנגלית: קובץ README.md נפתח ב-Lister באמצעות המקש F3, והטקסט (כולל סוגריים) מוצג נכון.

A bidi-override character reverses the display order of the text that follows it (the "Trojan Source" trick): the text between the brackets is "abc" followed by U+202E RIGHT-TO-LEFT OVERRIDE, then "evil", then U+202C POP DIRECTIONAL FORMATTING: [abc‮evil‬]. Expected display: `[abclive]`.

An explicit HTML direction:

<p dir="rtl">פסקה זו מסומנת במפורש כ-dir="rtl" ב-HTML, כך שהיא מיושרת לימין גם אם הייתה מתחילה באנגלית.</p>

<div dir="rtl">

- פריט ראשון ברשימה
- العنصر الثاني في القائمة
- Third item (English inside an RTL list: the bullets stay on the right)

</div>

---

## Indic, Southeast Asian and Caucasian scripts

### Hindi (हिन्दी, Devanagari)

सभी मनुष्यों को गौरव और अधिकारों के मामले में जन्मजात स्वतन्त्रता और समानता प्राप्त है। उन्हें बुद्धि और अन्तरात्मा की देन प्राप्त है और परस्पर उन्हें भाईचारे के भाव से बर्ताव करना चाहिए। Conjuncts and vowel signs must be shaped correctly: क्ष त्र ज्ञ श्र, कि की कु कू के कै को कौ, and the reph in धर्म and कर्म.

### Thai (ไทย)

มนุษย์ทั้งหลายเกิดมามีอิสระและเสมอภาคกันในเกียรติศักดิ์และสิทธิ ต่างมีเหตุผลและมโนธรรม และควรปฏิบัติต่อกันด้วยเจตนารมณ์แห่งภราดรภาพ Thai writes no spaces between words, so line breaking needs a dictionary. DirectWrite supplies one. Stacked tone marks must not overlap: กิ่ง ก้ำ ปื้น.

### Georgian (ქართული)

ყველა ადამიანი იბადება თავისუფალი და თანასწორი თავისი ღირსებითა და უფლებებით. მათ მინიჭებული აქვთ გონება და სინდისი და ერთმანეთის მიმართ უნდა იქცეოდნენ ძმობის სულისკვეთებით.

### Armenian (Հայերեն)

Բոլոր մարդիկ ծնվում են ազատ ու հավասար իրենց արժանապատվությամբ ու իրավունքներով։ Նրանք ունեն բանականություն ու խիղճ և միմյանց պետք է եղբայրաբար վերաբերվեն։

---

## Universal Declaration of Human Rights, Article 1

| Language | Text |
|----------|------|
| English | All human beings are born free and equal in dignity and rights. They are endowed with reason and conscience and should act towards one another in a spirit of brotherhood. |
| Русский | Все люди рождаются свободными и равными в своем достоинстве и правах. Они наделены разумом и совестью и должны поступать в отношении друг друга в духе братства. |
| Українська | Всі люди народжуються вільними і рівними у своїй гідності та правах. Вони наділені розумом і совістю і повинні діяти у відношенні один до одного в дусі братерства. |
| Deutsch | Alle Menschen sind frei und gleich an Würde und Rechten geboren. Sie sind mit Vernunft und Gewissen begabt und sollen einander im Geist der Brüderlichkeit begegnen. |
| Français | Tous les êtres humains naissent libres et égaux en dignité et en droits. Ils sont doués de raison et de conscience et doivent agir les uns envers les autres dans un esprit de fraternité. |
| Ελληνικά | Όλοι οι άνθρωποι γεννιούνται ελεύθεροι και ίσοι στην αξιοπρέπεια και τα δικαιώματα. Είναι προικισμένοι με λογική και συνείδηση, και οφείλουν να συμπεριφέρονται μεταξύ τους με πνεύμα αδελφοσύνης. |
| 中文 | 人人生而自由，在尊严和权利上一律平等。他们赋有理性和良心，并应以兄弟关系的精神相对待。 |
| 日本語 | すべての人間は、生れながらにして自由であり、かつ、尊厳と権利とについて平等である。人間は、理性と良心とを授けられており、互いに同胞の精神をもって行動しなければならない。 |
| 한국어 | 모든 인간은 태어날 때부터 자유로우며 그 존엄과 권리에 있어 동등하다. 인간은 천부적으로 이성과 양심을 부여받았으며 서로 형제애의 정신으로 행동하여야 한다. |
| العربية | يولد جميع الناس أحرارًا متساوين في الكرامة والحقوق. وقد وهبوا عقلاً وضميرًا وعليهم أن يعامل بعضهم بعضًا بروح الإخاء. |
| עברית | כל בני האדם נולדו בני חורין ושווים בערכם ובזכויותיהם. כולם חוננו בתבונה ובמצפון, לפיכך חובה עליהם לנהוג איש ברעהו ברוח של אחוה. |
| हिन्दी | सभी मनुष्यों को गौरव और अधिकारों के मामले में जन्मजात स्वतन्त्रता और समानता प्राप्त है। |
| ไทย | มนุษย์ทั้งหลายเกิดมามีอิสระและเสมอภาคกันในเกียรติศักดิ์และสิทธิ |
| ქართული | ყველა ადამიანი იბადება თავისუფალი და თანასწორი თავისი ღირსებითა და უფლებებით. |
| Հայերեն | Բոլոր մարդիկ ծնվում են ազատ ու հավասար իրենց արժանապատվությամբ ու իրավունքներով։ |

---

## Font fallback: rarer scripts

| Script | Sample | Notes |
|--------|--------|-------|
| Bengali | বাংলা ভাষা | conjuncts |
| Tamil | தமிழ் மொழி | |
| Telugu | తెలుగు భాష | |
| Gujarati | ગુજરાતી ભાષા | |
| Gurmukhi | ਪੰਜਾਬੀ ਭਾਸ਼ਾ | |
| Sinhala | සිංහල භාෂාව | |
| Khmer | ភាសាខ្មែរ | subscript consonants |
| Lao | ພາສາລາວ | |
| Myanmar | မြန်မာဘာသာ | complex reordering |
| Tibetan | བོད་ཡིག | vertical stacking |
| Ethiopic | አማርኛ | Amharic |
| Persian | زبان فارسی | RTL, Arabic script |
| Urdu | اردو زبان | often shown in Nastaliq style |
| Syriac | ܠܫܢܐ ܣܘܪܝܝܐ | RTL |
| Thaana | ދިވެހިބަސް | RTL, Dhivehi |
| Yiddish | ייִדיש | Hebrew script with points |
| Cherokee | ᏣᎳᎩ ᎦᏬᏂᎯᏍᏗ | |
| Canadian Syllabics | ᐃᓄᒃᑎᑐᑦ | Inuktitut |
| Mongolian | ᠮᠣᠩᠭᠣᠯ ᠪᠢᠴᠢᠭ | normally vertical |
| Math alphanumerics | 𝐀𝐁𝐂 𝒜ℬ𝒞 𝔄𝔅ℭ 𝕬𝕭𝕮 𝔸𝔹ℂ 𝙰𝙱𝙲 | supplementary plane (surrogate pairs) |
| Egyptian hieroglyphs | 𓀀 𓁐 𓃰 𓆣 𓂀 | supplementary plane, "Segoe UI Historic" |
| Musical symbols | 𝄞 𝄢 𝅘𝅥𝅮 ♩ ♪ ♫ ♬ | |
| Braille | ⠓⠑⠇⠇⠕ ⠺⠕⠗⠇⠙ | "hello world" |

---

## Emoji

Emoji-heavy paragraph 🎉: Today 🌅 I opened my laptop 💻, brewed coffee ☕, and started debugging 🐛 the renderer 🖼️. Tables 📊 were broken 💥, images 🏞️ were blurry 😵‍💫, and the emoji 😂🤣😭 rendered as boxes ⬜. After three hours ⏰ and two pizzas 🍕🍕, everything worked ✅! The team 👩‍💻👨‍💻🧑‍💻 celebrated 🥳🍾 and the cat 🐈‍⬛ slept 😴 through it all. Flags of our contributors: 🇺🇦 🇩🇪 🇯🇵 🇧🇷 🇮🇳 🇺🇸 🇫🇷 🇰🇷. Families: 👨‍👩‍👧‍👦 👩‍👩‍👦 👨‍👨‍👧‍👧. Skin tones: 👋🏻 👋🏼 👋🏽 👋🏾 👋🏿. Hearts: ❤️ 🧡 💛 💚 💙 💜 🖤 🤍 🤎 ❤️‍🔥 ❤️‍🩹.

Emoji at line ends test the vertical metrics: when a line holds only emoji, its height should not jump much compared to a line of text.

🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀🚀 (a long run of emoji with no spaces must still wrap)

---

## Symbols

**Math:** ∑ ∏ ∐ ∫ ∬ ∭ ∮ ∞ ≠ ≈ ≡ ≢ ≤ ≥ ≪ ≫ ∂ ∇ √ ∛ ∜ ∝ ∈ ∉ ∋ ⊂ ⊃ ⊆ ⊇ ∪ ∩ ∧ ∨ ¬ ⊕ ⊗ ∀ ∃ ∄ ∅ ℕ ℤ ℚ ℝ ℂ ℵ ⊥ ∥ ∠ △ ± ∓ × ÷ ⋅ ∘ ′ ″ ‴

**Formulas in plain Unicode:** ∫₀^∞ e^(−x²) dx = √π ⁄ 2, ∑ᵢ₌₁ⁿ i = n(n+1)/2, E = mc², H₂O, CO₂, x⁽ⁿ⁾, 10⁻⁶, a ≤ b ≤ c, ∀ε>0 ∃δ>0: |x−a|<δ ⇒ |f(x)−f(a)|<ε.

**Arrows:** ← → ↑ ↓ ↔ ↕ ↖ ↗ ↘ ↙ ⇐ ⇒ ⇑ ⇓ ⇔ ⟵ ⟶ ⟷ ⟸ ⟹ ↩ ↪ ↺ ↻ ⤴ ⤵ ➜ ➔ ➡ ⬅ ⬆ ⬇

**Box drawing in proportional text** (it won't line up here, and that's expected; see the code blocks in `04-code-highlighting.md`): ┌─┬─┐ │ ├─┼─┤ └─┴─┘ ╔═╦═╗ ║ ╚═╩═╝ ░▒▓█ ▀▄ ■□▪▫ ●○◆◇ ★☆

**Currency:** $ € £ ¥ ₽ ₴ ₹ ₩ ₪ ₺ ₿ ¢ ₫ ₦ ₱ ₲ ₵ ฿

**Letterlike and misc:** © ® ™ ℠ № ℃ ℉ µ Ω ℧ Å ℮ ℞ ℓ ⅋ § ¶ † ‡ • ‣ ◦ ‰ ‱ ° ☐ ☑ ☒ ✓ ✗ ♠ ♣ ♥ ♦ ☀ ☁ ☂ ☃ ☎ ☯ ☮ ⚠ ⚡ ♻

**Fractions:** ½ ⅓ ⅔ ¼ ¾ ⅕ ⅙ ⅛ ⅜ ⅝ ⅞ and with a fraction slash: 1⁄2, 3⁄4, 22⁄7

**Superscripts and subscripts:** x⁰ x¹ x² x³ x⁴ x⁵ x⁶ x⁷ x⁸ x⁹ x⁺ x⁻ x⁼ x⁽ x⁾ xⁿ xⁱ, and x₀ x₁ x₂ x₃ x₄ x₅ x₆ x₇ x₈ x₉ x₊ x₋ x₌ x₍ x₎ xₐ xₑ xₒ xₓ

**Ligatures:** plain "fi fl ffi ffl ft" (the font may ligate them) vs. presentation forms "ﬁ ﬂ ﬃ ﬄ ﬅ". Roman numerals: Ⅰ Ⅱ Ⅲ Ⅳ Ⅻ ⅰ ⅱ ⅲ. Circled: ① ② ③ ⑩ ⑳ Ⓐ ⓐ.

---

## Quotes, dashes and punctuation

| Style | Example |
|-------|---------|
| English | “double” and ‘single’ quotes, and it’s an apostrophe |
| German | „doppelt“ und ‚einfach‘; also »Guillemets« |
| French | « guillemets » et ‹ simples › |
| Russian | «ёлочки» и „лапки“: «Он сказал: „Привет“» |
| Polish | „cudzysłów” i »wewnętrzny« |
| Swedish | ”citat” och ’enkla’ |
| Japanese | 「かぎ括弧」と『二重かぎ括弧』 |
| Chinese | “中文引号”和「直角引号」 |
| Straight ASCII | "double" and 'single' (must stay straight: GFM does no smart-quote substitution) |

| Character | Name | Code point | In context |
|:---------:|------|------------|------------|
| - | hyphen-minus | U+002D | well-known |
| ‐ | hyphen | U+2010 | well‐known |
| ‑ | non-breaking hyphen | U+2011 | well‑known (never breaks here) |
| ‒ | figure dash | U+2012 | 555‒1234 |
| – | en dash | U+2013 | pages 10–20, 1990–2026 |
| — | em dash | U+2014 | wait—what? |
| ― | horizontal bar | U+2015 | ―quotation dash |
| − | minus sign | U+2212 | −40 °C, 5 − 3 = 2 |
| … | ellipsis | U+2026 | and so on… vs three dots... |
| · | middle dot | U+00B7 | l·l (Catalan) |
| ‽ | interrobang | U+203D | Really‽ |
| ¿ ¡ | inverted marks | U+00BF U+00A1 | ¿Qué? ¡Hola! |

GFM performs **no** "smart punctuation". Straight quotes "like these", `--` and `...` must render exactly as typed: -- and --- and ... stay ASCII.

---

## Combining characters

- Precomposed é (U+00E9) vs decomposed é (e + U+0301 COMBINING ACUTE ACCENT). The two should look **identical**: é é
- Multiple marks on one base: ạ̀̃ (a + grave + dot below + tilde)
- Vietnamese stacked diacritics: ệ ở ữ ặ ẫ Ỗ
- Decomposed Hangul jamo, which should render as one syllable: 한 (U+1112 U+1161 U+11AB = 한)
- Devanagari with nukta and virama: क़ ख़ ग़ क्‍ष
- Enclosing marks: 1⃣ A⃝ and x⃞
- Zalgo text, where many combining marks stack far above and below the line. Line height should not explode, and nothing may crash: Ḣ̝̱̀̅ͧ̂é͇͙̠̜̤̾ͫͅ c̪͎͖̀͛̚ͅo̬͓͛ͧm̱̬̪̥ͫ̓ͅe͇̳̣ͬ́͛̄̀̽ṣ͓̦̠̄ͧ̚,̠̮̘̺̬̾̇ͅ t̮̠̋̑ͅh͈̰͕ͦ͆ͅê̼̭̬̞̤̺͒ͨ̊͐ Ż̥̱̝̣ͣ̆͗͆ȃ̦̙̭̘̩̉̔͐̄̅l̫̻̥̝̹̅ͮ̔̓͑̄̚g̲̦͉ͯ͑͛ő͎̠͈͓ͧ̍̓̃
- The user-supplied Zalgo sample: Z̤͔ͧ̑̓ä͖̭̈̇lͮ̒ͫǫ̗̻

---

## Invisible and special-width characters

Each example sits between visible markers `[` and `]`. None of the invisible characters should produce a visible glyph or an empty box.

| Character | Code point | Example | Expected |
|-----------|------------|---------|----------|
| Zero-width space | U+200B | [a​b] | looks like "ab", but a line may break there |
| Zero-width non-joiner | U+200C | [ف‌ل] and [می‌خواهم] | stops Arabic joining |
| Zero-width joiner | U+200D | [👩‍💻] | joins into one emoji (👩‍💻) |
| Word joiner | U+2060 | [a⁠b] | invisible, no break opportunity |
| BOM / ZWNBSP mid-text | U+FEFF | [a﻿b] | invisible |
| Left-to-right mark | U+200E | [a‎b] | invisible |
| Right-to-left mark | U+200F | [a‏b] | invisible |
| Soft hyphen (literal) | U+00AD | [ab­cd] | invisible unless the word breaks there |
| Soft hyphen (`&shy;`) | U+00AD | [ab&shy;cd] | same as above |
| Variation selector-16 | U+FE0F | [☺️] vs [☺] | emoji vs text presentation |
| No-break space | U+00A0 | [a b] and [a&nbsp;b] | a normal-width space that never breaks |
| Narrow no-break space | U+202F | [a b] | a narrow space |
| En space | U+2002 | [a b] | half an em wide |
| Em space | U+2003 | [a b] | one em wide |
| Thin space | U+2009 | [a b] | thin |
| Hair space | U+200A | [a b] | very thin |
| Figure space | U+2007 | [1 000] | as wide as a digit |
| Ideographic space | U+3000 | [文　字] | full CJK width |

**No-break spaces in context.** These pairs must never be split across lines: Windows 11, 100 km, § 5, Dr. Smith, 10&nbsp;000&nbsp;000, and U+2011 in 555‑0199.

**Soft hyphens in context.** Resize the window: these long German words may break *only* at the soft hyphens, and a visible hyphen appears at each break. Donau&shy;dampf&shy;schiff&shy;fahrts&shy;gesell&shy;schafts&shy;kapitän, Rind&shy;fleisch&shy;etikettierungs&shy;überwachungs&shy;aufgaben&shy;übertragungs&shy;gesetz, Grund&shy;stücks&shy;verkehrs&shy;genehmigungs&shy;zuständigkeits&shy;übertragungs&shy;verordnung, Kraft&shy;fahrzeug&shy;haftpflicht&shy;versicherung, Arbeiter&shy;unfall&shy;versicherungs&shy;gesetz, and Unab­hängig­keits­erklärung (literal U+00AD).

---

## Long words and line breaking

A single 300-character "word" with no break opportunities. It must not overflow the window: it has to be broken somewhere or scroll, and it must not be clipped silently.

LopadotemachoselachogaleokranioleipsanodrimhypotrimmatosilphiokarabomelitokatakechymenokichlepikossyphophattoperisteralektryonoptekephalliokigklopeleiolagoiosiraiobaphetraganopterygonPneumonoultramicroscopicsilicovolcanoconiosisHippopotomonstrosesquippedaliophobiaSupercalifragilisticexpialidociousAn

A long URL, which is autolinked and must wrap: https://www.example.com/some/extremely/long/path/segments/that/go/on/and/on/without/any/spaces/at/all/index.html?utm_source=markdown&utm_medium=viewer&utm_campaign=long-url-wrapping-test&session=0123456789abcdef0123456789abcdef&page=42#section-with-a-rather-long-fragment-identifier

The same URL as inline code: `https://www.example.com/some/extremely/long/path/segments/that/go/on/and/on/without/any/spaces/at/all/index.html?utm_source=markdown`

A long word with zero-width spaces, which may break at each ZWSP: super​cali​fragi​listic​expi​ali​docious​super​cali​fragi​listic​expi​ali​docious​super​cali​fragi​listic​expi​ali​docious

A long run of CJK without spaces, which may break between any two ideographs: 中华人民共和国宪法是国家的根本法具有最高的法律效力全国各族人民一切国家机关和武装力量各政党和各社会团体各企业事业组织都必须以宪法为根本的活动准则并且负有维护宪法尊严保证宪法实施的职责中华人民共和国宪法是国家的根本法具有最高的法律效力

Hyphenated words break after the hyphen: state-of-the-art, well-known-and-widely-used-command-line-tool-for-searching-text, and Jean-Baptiste-Alphonse-Karr.

---

## Formatting across scripts

| Script | Bold | Italic | Bold italic | Code | Strike |
|--------|------|--------|-------------|------|--------|
| Latin | **Bold** | *Italic* | ***Both*** | `code` | ~~strike~~ |
| Cyrillic | **Жирный** | *Курсив* | ***Оба*** | `код` | ~~зачёркнутый~~ |
| Greek | **Έντονα** | *Πλάγια* | ***Και τα δύο*** | `κώδικας` | ~~διαγραφή~~ |
| Chinese | **粗体** | *斜体* | ***粗斜体*** | `代码` | ~~删除线~~ |
| Japanese | **太字** | *斜体* | ***太字斜体*** | `コード` | ~~取り消し線~~ |
| Arabic | **عريض** | *مائل* | ***كلاهما*** | `رمز` | ~~شطب~~ |
| Hebrew | **מודגש** | *נטוי* | ***שניהם*** | `קוד` | ~~קו חוצה~~ |
| Hindi | **मोटा** | *तिरछा* | ***दोनों*** | `कोड` | ~~काटना~~ |

CJK fonts usually have no italic face, so the italic column may show synthetic slanting or no slant at all. Intraword `*` emphasis works in CJK without spaces: 这是*强调*的文字 and これは**重要**です.

---

## Headings in mixed scripts

# H1: English — Русский — 日本語

## H2: Ελληνικά — العربية — עברית

### H3: 한국어 · ไทย · हिन्दी

#### H4: ქართული · Հայերեն · Ўзбекча

##### H5: Emoji 🚀 and symbols ∑∞ in a heading

###### H6: Tiếng Việt — Čeština — Polski — Türkçe

## مرحبا بالعالم: an Arabic heading (RTL)

## שלום עולם: a Hebrew heading (RTL)

## 这是一个非常长的中文标题，用于测试标题换行是否正确，当窗口宽度不够时应该自动换行而不是溢出

[Back to top](#typography-and-internationalization)
