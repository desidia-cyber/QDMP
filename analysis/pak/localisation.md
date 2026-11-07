# Localisation — English text catalogue

Source: `QueensDomain/Content/Localization/Game/en/Game.locres`
(293526 bytes, extracted from the pak at offset 0x0 + 53, sha1-verified).

Also decoded for reference: `Engine/Content/Localization/Engine/en/Engine.locres`
(1596075 bytes, 15769 strings) — engine/editor text, not game text.

**2329 English strings** were recovered. That is the complete Game namespace: the file is only
286 KB, so this really is everything the game localises. Most of the game's narrative is *not* in
here — see "What is and is not in this file" below.


## Format

Not stock UE4 locres. Both files start with the same 13 bytes:

```
0x00  u32  magic 0x7574140E          (bytes 0E 14 74 75)  -- this part IS stock
0x04  u8[13] 67 4A 03 FC 4A 15 90 9D C3 37 7F 1B 03
             byte 0x04 is where stock UE would put a version byte (0x67 = 103, not a stock
             version); the following 12 bytes are byte-identical between Game.locres and
             Engine.locres, so they are a fixed build stamp, not data.
0x11  i64  string_table_offset       85689 (Game) / 862401 (Engine)
0x19  u32  declared string count     2388 (Game) / 20149 (Engine)
0x1D  ...  namespace/key records, then the string table at string_table_offset
```

The **string table** is `repeat { u32 refcount ; FString text }` from
`string_table_offset` to EOF. `FString` is `i32 length` + bytes; a **negative length means
UTF-16LE** and the byte count is `2*|length|`. The strings are stored in case-insensitive
alphabetical order and are de-duplicated — the `refcount` is how many keys share each string
(`"Get key from librarian"` has refcount 7). Both files parse to EOF with at most 8 slack bytes,
which is the validation.

The **namespace/key records** before the table use 32-hex-digit identifiers
(`CBC3CDBC4C8C56F0A3F0EEB17FBB687F`) for imported text, and human-readable names for the
hand-authored text (`Dialogue`, `ImportedStringTable`, `inha_j_01_desc`,
`cha_quest_shop_unlocks_name`, `binding_a`). The record stride is 49 bytes for the GUID-named
group (`u32`,`u32`,`u32`,`FString`) but the region is not uniform, and **the exact
key -> stringindex pairing was not recovered** — see "Not recovered" below.


## The catalogue (2329 strings)

Sorted as stored (case-insensitive alphabetical). `xN` = number of keys sharing that string. Newlines are shown as `\n`.

One artefact to read correctly: row 0's `refs` value (2329) is **not** a sharing count — it is the table's own declared record count, which happens to sit where row 0's prefix word would be. Every other row's value is a genuine sharing count (`"Get key from librarian"` is shared by 7 keys).

| # | refs | text |
|---:|---:|---|
| 0 | 2329 |  Pierce + Poison |
| 1 | 1 | "{OBJECT}" Attempted to Activate a null pointer in Activation list Index "{index}" THIS IS A BUG! Please report!! |
| 2 | 1 | <img id="DefaultConfirmButton"></> |
| 3 | 1 | <img id="PSConfirmButton"></> |
| 4 | 1 | AllCorrect |
| 5 | 1 | Axis Conflict Found. Axis: {Axis} Index: {Index} Key: {Key} |
| 6 | 1 | B33SMENTSUPPER |
| 7 | 1 | Cheat Stick |
| 8 | 1 | Cheat stick |
| 9 | 1 | Compiling {num} Materials |
| 10 | 1 | Dir light Intensity: {val} |
| 11 | 1 | Eric Berger [Biogenesis] & Bradley Boothe [Birdlay] |
| 12 | 1 | Found {num} shader compile dependency packages |
| 13 | 1 | Freshly Baked Games LLC |
| 14 | 1 | Here take this spooky item |
| 15 | 1 | Reset Achievements |
| 16 | 1 | Test |
| 17 | 1 | There are unapplied Resolution or Window Mode changes.\n\nWould you like to apply them? |
| 18 | 1 | Tryign TO Fade out Dir Ambient Light |
| 19 | 1 | Tryign To Fade IN Dir Ambient Light |
| 20 | 1 | Updating Quest State From {cur} to {next} |
| 21 | 1 | [] |
| 22 | 1 | [{key}] |
| 23 | 1 | b |
| 24 | 1 | bird test zone |
| 25 | 2 | castle |
| 26 | 1 | main area |
| 27 | 1 | pls pls pls pls pls plspls pls plspls pls pls |
| 28 | 1 | texttexttexttexttexttexttexttexttext |
| 29 | 1 | this only shows in editor builds for debugging |
| 30 | 1 | {loc} - {area} |
| 31 | 1 | {num}. |
| 32 | 1 | □○△× Button |
| 33 | 2 | Find the two keys. One is at the Tower of Knowledge. |
| 34 | 1 | GO AWAY |
| 35 | 1 | GO AWAY I DON'T HAVE ANY DIALOGUE |
| 36 | 7 | Get key from librarian |
| 37 | 1 | Get ley from librarian |
| 38 | 1 | What do you want? |
| 39 | 1 | What's up? |
| 40 | 1 | bleh |
| 41 | 1 | fine take this key |
| 42 | 1 | i die now |
| 43 | 1 | take this book, I can't stop reading it! |
| 44 | 1 | "Give me treats!" |
| 45 | 1 | "I love chicken!" |
| 46 | 1 | ' |
| 47 | 1 | + |
| 48 | 1 | +{num}% |
| 49 | 1 | , |
| 50 | 1 | - |
| 51 | 2 | . |
| 52 | 1 | / |
| 53 | 1 | 12-7\n\nOh Wourha, how I long to room with you, my most beloved of Knowledge Keepers. I have searched the tower high and low for the right book to give you, one that would truly show the depth of my feelings! |
| 54 | 1 | 17-7\n\nToday I awoke to the horrid screaming of Yatiop, another of Siraatsu's servants. I ran straight away to her chambers and found that she had a large gash from chin to forehead. She explained to me that Siraatsu had not been at all pleased with the breakfast she was served. I must get out of this dreadful castle.\n\n3-8\n\nAfter a relatively calm few weeks, another fearful incident has occurred! My dear friend Naer told me a terrible rumor. The queen is apparently manufacturing mechanical servants and we will all be unemployed or worse within a month's time. I wanted to leave anyway, in  …(truncated) |
| 55 | 1 | 19-4\n\nHead Sin Keeper Huokdo has recently taken an artifact from storage, it appears to be a large silver egg originated from the Dekanima. I feel a bit uneasy about this, artifacts from the Dekanima are quite dangerous and keeping them under lock and key is one of our primary duties here. Perhaps it was merely due for some sort of inspection.\n\n3-5\n\nHuokdo and a small group of other Keepers have been performing daily rituals around this egg for several weeks now. Signs of the Dekanima have begun to show around the tower, what in the world are they doing? I have begun preparations to aban …(truncated) |
| 56 | 1 | 2-7\n\nToday we begin our pilgrimage through the ancient Deimpo Mausoleum. Through these winding corridors and chambers are many graves of saints, legends, and even shrines to the great god Wodaso and goddess Puwema.\n\nKing Gius Abraon and a company of knights are here as well, venturing into the depths in some quest or another, though they do not speak of the details of their purpose here. The knights offered us escort down the main stairway, but from there it seems our paths diverge. No matter, the mausoleum is relatively safe and we should be fine on our own as generations of pilgrims were …(truncated) |
| 57 | 1 | 2-8\n\nYesterday was a disaster, a true nightmare. I brought the book I had carefully selected to Wourha's office and presented it to her. For a moment my heart fluttered as she handed me a book in return! This book though... is not what I had hoped for at all. It's a dark and grotesque book describing the most demented and horrifying things I've ever read of.\n\nWhy in the world did Wourha think a girl like me would want to read such a thing? She even urged me NOT to read the incantations from the back of the book aloud. If this is the kind of thing that interests her, perhaps I will do it an …(truncated) |
| 58 | 1 | 3-12\n\nOkmaru and I have finally arrived at this most sacred shrine dedicated to lord Jigeki. We took the day to pray and make ritual in his name before this statue of his holy visage. There was a small metal piece on the altar here resembling part of Jigeki's symbol, Okmaru has insisted on taking it with us. \n\nTomorrow we set out for the cavern beyond here, we'll have to pass by goddess Puwema's temple on the way. I wanted to pay homage to her as well, the great goddess is Jigeki's counterpart after all, but Okmaru insists we keep our distance. Her beliefs are not quite the same as mine, a …(truncated) |
| 59 | 1 | 3-13\n\nWe'd made our way through the ruins past Puwema's temple and down into the caverns when suddenly the water became quite violent. I began to rush for this small outcropping of land but Okmaru seemed frozen in place, a look of terror on her face. I was screaming for her to follow when suddenly the silt beneath her feet began to shift and bubble and a tremendous fish erupted from the surface. \n\nI could hardly believe it, the fish had swallowed Okmaru whole and sunk back beneath the silt, I don't know what to do. I can't bring myself to go back into the water yet there is no other way ou …(truncated) |
| 60 | 1 | 30-7\n\nI found it! A most perfect book that would truly show Wourha all that I cannot bring myself to speak to her aloud! I will bring her the book tomorrow and then surely she will come running into my arms. |
| 61 | 1 | 4-26\n\nToday a fortune teller told me I would encounter the wheel of fortune deep beneath the ground on a sacred isle. When I asked her for clarification she just... winked at me and made crude hand gestures. What an odd fortune, only a fool would get their hopes up from something so vague.\n\n4-27\n\nI encountered a ragged man in an alleyway who told me something quite strange. He recounted a tale of a beautiful woman who spins fine thread and greatly pleases any man who rings her bell and offers coin. He said this woman lives in a cavern deep below the Isle of Neasied. This can't be a coinc …(truncated) |
| 62 | 1 | 4-7\n\nGods save us, the dead have come alive and all manner of horrors have appeared. We lost several today in the chaos, I pray Puwema protects us while we rest in her holy shrine.\n\nThe echoes of the knights' voices carry here from some chamber further inside, our only hope lies in rejoining them. Going back the way we came would be far too dangerous. |
| 63 | 1 | 5-22\n\nI've been down here for nearly a month and sure enough the waters in this cavern are slowly but surely becoming tainted by something most foul. Today I made my way into this sealed room which appears to be some sort of shrine dedicated to the god Jigeki, yet the figure depicted is an Orbewa woman. It seems to be quite new, much more recent than the statues depicting Yubior as the avatar of Jigeki. \n\nI'm not sure what exactly to make of this, it seems quite likely that the befouled waters and this shrine are connected somehow. I really should end this expedition soon and return to the …(truncated) |
| 64 | 1 | ; |
| 65 | 1 | <img id="PSCircle"></>Button |
| 66 | 1 | <img id="PSCross"></>Button |
| 67 | 1 | <img id="PSSquare"></>Button |
| 68 | 1 | <img id="PSTriangle"></>Button |
| 69 | 1 | = |
| 70 | 1 | ??? |
| 71 | 2 | A |
| 72 | 3 | A Blood Angel risen from the Dekanima ritual pit in the Verdant Hollow, nothing evades her watchful eye.\n\nThese Josami walk between two worlds, their bodies regenerate through blood magic and their souls able to return to the body after mortal wounds are suffered. |
| 73 | 1 | A Blood Angel risen from the Dekanima ritual pit in the Verdant Hollow, she fights like she's got nothing to lose.\n\nThe Josami Blood Angels were enhanced by Queen Siraatsu in a ritual involving blood sacrifice at a gateway to the Dekanima. |
| 74 | 1 | A Blood Angel risen from the Dekanima ritual pit in the Verdant Hollow, she is a crazed fighter who wields a shredding mechanical mace. \n\nThe powerful twin lords that once lead this Josami unit were so dangerous, the queen had them locked away in a long forgotten dungeon. |
| 75 | 1 | A Blood Angel risen from the Dekanima ritual pit in the Verdant Hollow, she is a tactical fighter who wields a spear and a large magical shield.\n\nUnfortunately for Siraatsu, the Blood Angels are too difficult to control to be tactically effective. After each temporary death, their souls return changed. Madness and rage overtaking their minds, just what do they experience on the other side? |
| 76 | 1 | A Fine Collection |
| 77 | 1 | A Game By |
| 78 | 1 | A Kingdom Free From Darkness |
| 79 | 1 | A Lifetime's Work |
| 80 | 1 | A Rustic necklace crafted by the Orbewa people. The wood and beads have been soaked in a variety of oils extracted from folk remedies found deep in the ocean. Wearing it provides a small resistance to many common ailments.  |
| 81 | 1 | A automaton performing drilling and repair work on Castle Siraatsu, it's been programmed to prioritize removal of unwanted visitors. |
| 82 | 1 | A beastly slithering insect, covered in hard shells and bearing fangs on both ends. \n\nRumor has it that a long-lost cult of warriors raised these creatures and lived symbiotically with them. |
| 83 | 1 | A beastly slithering insect, covered in hard shells and bearing fangs on both ends. \n\nThis variant species carries a weakening toxin and prefers a nice, damp environment. |
| 84 | 1 | A biography of Ikku, the current head sage of the Abraon royal family. It recounts his centuries-long life, in excruciating detail. Apparently the sage is quite fond of shrimp from the western continent, you never would have guessed. |
| 85 | 1 | A biography of Queen Siraatsu detailing the bizarre events of her life. According to the tales within, Sipeha came to Siraatsu and inhabited her while she lived on a vessel that journeyed through the void of space many millennia ago. \n\nIt goes on to detail Siraatsu's rule over another world and her role in the construction and relocation of humanity to the world you now inhabit. \n\nYou find this nearly impossible to believe and yet, could magic alone explain what you've seen in the ancient world found deep beneath the Isle of Neasied? |
| 86 | 1 | A black and white blade with a distinctive red streak. Attached to the pommel is a charm resembling the lovable mascot Pyp.\n\nWhen charged it can unleash a bright yellow wave of radioactive energy. |
| 87 | 1 | A black mechanical gargoyle bearing a pike tipped with the symbol of Wodaso. \n\nIt was instructed to guard this small temple of the god Wodaso by the Sin Keepers, for it holds forbidden secrets. |
| 88 | 1 | A blood red key forged in the style of Josami armor. |
| 89 | 1 | A body dumped unceremoniously underground, it awoke twisted and malformed with all manner of foliage springing from its back. \n\nIt crawls about in a haze of anger and confusion attacking anything in sight. |
| 90 | 1 | A book about preparing food and hosting leisurely activities for humans. Why is it written from this context?\n\nNo matter, the recipes sound extremely delicious and you would love to try them. |
| 91 | 1 | A book about the Kingdom of Ommu located on the southern continent. Ommu was allied with the Kingdom of Muojwi in the war many millennia ago. A few centuries ago they declared war on the Kingdom of Bamaen and lost that war as well. Despite relative peace, there is still tension between Ommu and Bamaen. |
| 92 | 1 | A book about the life of Yubior, avatar of the god Jigeki. \n\nThe last recorded tale takes place only ten years ago, with Yubior venturing into the caverns beneath the Obmuer Temple. |
| 93 | 1 | A book containing tales of dragons and their representations throughout history.\n\nAccording to this book these creatures do not exist, though you would swear you saw one once  |
| 94 | 1 | A book detailing Goseus, avatar of Wodaso's exile from this world. It seems he travelled deep under the world and constructed something called the moon. Within this moon he built enormous structures which house the souls of the dead until they can be reborn. This was done out of necessity and as penance for continuing to engage in the use of forbidden technologies. \n\nThe events described are a bit over your head.  |
| 95 | 1 | A book filled with hundreds of magical short stories to set the imagination alight. \n\nEach tale appears to contain a relatively timeless moral lesson of one kind or another. \n\nYou fondly remember reading these stories in your youth, though it seems many did not have such an opportunity. |
| 96 | 1 | A book of strange tales describing impossible spaces and unimaginable beings from other dimensions.\n\nIt appears to include diagrams and spells for invoking such beings and travelling to such places, certainly no one would be foolish enough to actually try this though. |
| 97 | 1 | A book of tales from the eastern continent, home of the lost Kingdom of Epha. In modern times the continent is inhabited by loose knit communities known as the Idinti Wanderers. Resources on the eastern continent are scarce as the landscape is mostly desert. Littered with forests of bone trees and multiple species of gargantuan spiders, it seems extremely harsh. A few mentions of ancient Epha structures hidden among the sands set your imagination ablaze, you daydream for a moment of the wonders they might contain. |
| 98 | 1 | A book that documents the life and death of Queen Siraatsu, avatar of the goddess Sipeha.\n\nShe is reported to have extended her life with black magic and ruled over all the lands in the world for hundreds of years. Her attempt at eternal rule was ended with her defeat at the hands of an unnamed avatar of Puwema many thousands of years ago. |
| 99 | 1 | A book that purports to catalogue over one thousand species of ratty bugs from across the world.\n\nOne variant is described as bearing a large metallic structure on its back for the purpose of increased aerodynamics while racing. You very much doubt the existence of many of the species described within.  |
| 100 | 1 | A book that records pivotal battles of a war between the Kingdom of Epha and the Kingdom of Muojwi.\n\nEpha which was allied with your homeland of Bamaen seems to have won the war at great cost. Both the Epha and Muojwi kingdoms fell many thousands of years ago as a result. |
| 101 | 1 | A bottle of holy water from a hidden spring in the Kingdom of Epha. The location of this spring is a well kept secret, even among the rare explorer who would venture to such a distant region.\n\nSipping this effervescent liquid is a rare treat, sending a powerful surge of energy through your body that restores your magical energies. |
| 102 | 1 | A bracelet from another reality, it was originally found in a tower of forgotten nightmares. \n\nIt protects its wearer from harm a single time and then shatters. It has been blessed with an enchantment that restores it after a short time passes.  |
| 103 | 1 | A bright golden crab, very rare and could fetch quite a high price. \n\nYou wonder if it will be eaten or kept as a pet. |
| 104 | 1 | A bright red crystalline egg held in a black metal stand.\n\nUsed in Josami blood rituals, you can feel an evil power emanating from within. |
| 105 | 1 | A brilliant azure orb radiating an unusual energy. \n\nIt feels as if you catch a glimpse of another world within. |
| 106 | 1 | A brilliant green dragon who was raised by Usunha. She will burn any who dares threaten her master with blazing breath of azure flame. |
| 107 | 1 | A bubbling mass of flesh and sharp bones surrounding a gaping maw. It spews waves of foul slime that inflict nearly every known ailment.  |
| 108 | 1 | A bubbling mass of metallic flesh and sharp bones surrounding a gaping maw. It spews waves of azure flame. \n\nForbidden rituals performed in the castle have weakened the barrier between this world and the Dekanima, allowing it to slip through. |
| 109 | 1 | A bubbling mass of sticky white flesh and sharp bones surrounding a gaping maw. It spews waves of putrid sticky slime.\n\nYou'll want to have a healthy supply of soap on hand if you're to handle such a creature. |
| 110 | 1 | A cannon that fires a long range continuous particle beam. It's equipped with blades at the front for close range thrusting attacks.\n\nIt requires energy to operate. |
| 111 | 1 | A catfish-like Orbewa citizen, it spews waves of deadly poison from its wide mouth. \n\nThese Orbewa were blessed by the priestess Muagyo with an incantation of Jigeki's toxic magic. |
| 112 | 1 | A ceremonial stone tiara decorated with flowers and a seashell. Traditionally when an Orbewa man wishes to propose, he must offer a tiara decorated with flowers and shells to the maiden he fancies.\n\nIf the selection and arrangement of flowers are not to the maiden's liking however, little will come of the offering.  |
| 113 | 1 | A chalice made from a hollowed out skull, filled to the brim with bubbling rot.\n\nNo matter what, you must not spill it. |
| 114 | 1 | A chalice made from a hollowed out skull, filled to the brim with churning poisonous goop.\n\nWhile the color is quite alluring, you do not dare to take a sip.  |
| 115 | 1 | A charm that can be attached to the Mask of Second Sight.\n\nIt will help you find Data Modules. |
| 116 | 1 | A charm that can be attached to the Mask of Second Sight.\n\nIt will help you find Meals left by Chef Gagi. |
| 117 | 1 | A charm that can be attached to the Mask of Second Sight.\n\nIt will help you find Orbewa Eggs. |
| 118 | 1 | A charm that can be attached to the Mask of Second Sight.\n\nIt will help you find Star Lizards. |
| 119 | 1 | A charm that can be attached to the Mask of Second Sight.\n\nIt will help you find Strange Silver Etched Skulls. |
| 120 | 1 | A charm that can be attached to the Mask of Second Sight.\n\nIt will help you find Treasure Chests. |
| 121 | 1 | A chicket species found deep underground, its rotten flesh oozes sickening disease. It smells quite foul, attracting swarms of flies. |
| 122 | 1 | A citrine statuette depicting Yubior, the avatar of the rotten god Jigeki. |
| 123 | 1 | A clay jar, crudely affixed to the end of a stick. \n\nA makeshift weapon at best. Swinging it produces a small rattling sound. |
| 124 | 1 | A close relative of the otherworldly plants of the Dekanima, its permanent gaze is always watching.\n\nFires a debilitating beam at any human that falls under its gaze.  |
| 125 | 1 | A club formed by twisted branches of living wood holding an immense verdant orb.\n\nThe Goddess Puwema's magic emanates from the orb, bathing the wielder in a rejuvenating light. |
| 126 | 1 | A club hewn from a rare dark branch. \n\nIt may appear crude at a glance however it's finely crafted weaponry for those with specific tastes. |
| 127 | 1 | A club hewn from a thick tree branch, leather wrapped around the thinner end to form a grip of sorts. \n\nDifficult to wield but certainly capable of a solid blow.  |
| 128 | 1 | A coating of acid has been added, allowing it to dissolve growths of soft tissue spawned by the Necronomicon. |
| 129 | 1 | A common hand axe meant for chopping wood, it can also make an effective weapon in the right hands. |
| 130 | 1 | A common lantern which provides little light. When exploring dark places it's better than nothing. |
| 131 | 1 | A common looking data module, you should bring to Ossi so he can analyze its contents. |
| 132 | 1 | A common soldier's armament kept in storage for many years. \n\nOffers a long reach and little else. |
| 133 | 1 | A common soldier's armament left to decay on the battlefield. \n\nOffers a long reach and little else. |
| 134 | 1 | A compact Energy Cell that holds half as much energy.\n\nIt's hard to fathom how much more advanced the civilizations of the past seem to have been. What could have driven them so deep underground? |
| 135 | 1 | A complex mechanical arm. It has the name "Peha" etched into it.  |
| 136 | 1 | A complex mechanical hammer from an age lost to time. It can discharge a wave of energy along the ground. |
| 137 | 1 | A complicated looking data module, you should bring to Ossi so he can analyze its contents. |
| 138 | 1 | A copper pendant in the shape of the symbol of god Wodaso.  The holy magenta gems affixed emit magic that greatly speeds physical improvement. |
| 139 | 1 | A crown befitting an emperor yes. You will wear it well. |
| 140 | 1 | A crown worn by Dagoom the Sea King, gifted to you by Erma for helping her recover the lost Orbewa Eggs.\n\nEnchanted by royal Orbewa magic which increases the speed of holy magic accumulation. |
| 141 | 1 | A crumbling fragment of stone, carved in the shape of a wing.\n\nHold it high above your head and you'll find yourself whisked away to a Wing Altar.\n\nStone fragments will crumble when travelling and are quite uncommon, so make sure to use them wisely. |
| 142 | 1 | A crystalline skull with twisted horns and unusual gems embedded. |
| 143 | 1 | A cudgel shaped from the wood of a tree found only where the cold winds blow. A faint scent of pine smoke lingers in the air around it, reminding you that it's not quite from around here.\n\nWhen the magic held in the gleaming gem affixed above its handle is discharged, it produces a wave of noxious poison, dispelled from within the wielder's own mind, calming the soul. |
| 144 | 1 | A cure-all potion sold by traders from the western kingdom of Ritudeon. \n\nDistilled from highly potent plants that grow only in the dense jungle at the center of the continent. |
| 145 | 1 | A dark looking data module, you should bring to Ossi so he can analyze its contents. |
| 146 | 1 | A dark stained fragment of bone said to be from the skeletal remains of Ruunen, avatar of Puwema. \n\nIt was used in an unholy necromantic ritual, you pray it was not authentic or the ritual's power would likely be immeasurable. |
| 147 | 1 | A deer-like insectoid creature bearing majestic horns and a deadly stinger. \n\nThough it has wings, it's a bit too heavy for sustained flight. |
| 148 | 1 | A detailed account of the Dekanima lies within the worn pages of this book. It describes an entire dimension whose makeup and rules are unlike those of our own. It describes an infinite expanse of sentient shifting metallic landscapes inhabited by equally unfathomable flora and fauna. Within this dimension is a being known as Okbujo, who seems to be something akin to a god.\n\nThe second half of the book details rituals, spells, and incantations for working the magic of the Dekanima, you dare not read them. |
| 149 | 1 | A detailed account of the Orbewa people's history and practices. Apparently they emerged from the sea only a few centuries ago and immediately were at war with the Kingdom of Bamaen for engaging in several forbidden practices. The details of these practices are unclear, it seems they were quickly bested and signed a peace treaty with Bamaen after just 2 months of war. |
| 150 | 1 | A diamond shaped metal plate with a cobalt blue orb of glass embedded in its center. \n\nUnseals doors to the generator towers by emitting a short esoteric song and magical light when merely holding it before them. |
| 151 | 1 | A diamond shaped metal plate with a glass cup embedded in its center. \n\nUnseals cup doors in Siraatsu Castle. |
| 152 | 1 | A diamond shaped metal plate with a glass pentacle embedded in its center. \n\nUnseals pentacle doors in Siraatsu Castle. |
| 153 | 1 | A diamond shaped metal plate with a glass sword embedded in its center. \n\nUnseals sword doors in Siraatsu Castle. |
| 154 | 1 | A diamond shaped metal plate with a glass wand embedded in its center. \n\nUnseals wand doors in Siraatsu Castle. |
| 155 | 1 | A diamond shaped metal plate with a teal green orb of glass embedded in its center. \n\nUnseals certain doors by emitting a short esoteric song and magical light when merely holding it before them. |
| 156 | 1 | A diamond shaped metal plate with an orange ocre orb of glass embedded in its center. \n\nUnseals certain doors by emitting a short esoteric song and magical light when merely holding it before them. |
| 157 | 1 | A diamond shaped stone key plated with bronze and lined with blood red stone. \n\nUmbada purchased this from a traveller named Taebba and assures you it is quite valuable. |
| 158 | 1 | A diamond shaped stone key plated with gold and lined with bright green jade. \n\nFound in a treasure vault, it seems it may unlock a door to yet more treasure. |
| 159 | 1 | A diamond shaped stone key plated with silver and lined with blue velvet. \n\nFound in a treasure vault, it seems it may unlock a door to yet more treasure. |
| 160 | 1 | A dish served to only the most refined gourmets. \n\nCrisp thin crust topped with red sauce, mounds of fresh cheese, and fine western herbs, baked until bubbling and slightly browned. \n\nThe unique combination of ingredients improves your ability to heal.  |
| 161 | 1 | A distinguished looking brass monocle worn by a docile gargoyle in the Tower of Knowledge.\n\nWhile you might assume it wore this monocle to assist in reading books, the reality is the lens has fallen out and  it must simply be a fashion statement.   |
| 162 | 1 | A dreadful beast with the body of a lion, a scorpion's tail, and the face of a man. \n\nIt was sent to the Tower of Knowledge by Queen Siraatsu to retrieve the Necronomicon.  |
| 163 | 1 | A dreadful creature that stalked the Dekanima, cloaked in a light-bending coat of metallic skin.\n\nThe Sin Keepers experimented with forbidden knowledge they were sworn to conceal. They paid dearly for this and now the Dekanima spills through into the Record of Sin. |
| 164 | 1 | A duck carved from a strange ancient stone.\n\nCertainly Umbada would find this fascinating. |
| 165 | 1 | A duck figurine cast from fine silver.\n\nIt seems likely that Umbada will enjoy it. |
| 166 | 1 | A duck figurine constructed from fine metal work. It softly emits a metallic hum, how peculiar. \n\nUmbada simply must see this.  |
| 167 | 1 | A dull brown crystalline egg held in a green metal stand.\n\nUsed in occult Josami rituals, you can feel an evil power emanating from within. |
| 168 | 1 | A fearful little lizard that clutches a bag of treasure in its tiny hands.\n\nA rare type that lives deep underground in places long forgotten by most. Oh the sights these lizards must have seen. |
| 169 | 1 | A fearful little lizard that clutches a bag of treasure in its tiny hands.\n\nIts unique star shaped tail and frill may be its namesake but you'll remember it for its dreadfully loud scream.  |
| 170 | 1 | A fearful little lizard that clutches a bag of treasure in its tiny hands.\n\nQuite the macabre type, it spends its days hanging out near graves. Known for its distinctive pale face paint and refusal to conform with the broader Star Lizard society. |
| 171 | 1 | A fearful little lizard that clutches a bag of treasure in its tiny hands.\n\nThis subspecies has adapted itself for wetter environments and certainly enjoys a nice relaxing bath after a long day at the office. |
| 172 | 1 | A fearful little lizard that clutches a bag of treasure in its tiny hands.\n\nThis subspecies lives deep underground, in a world unlike most people have ever dreamed possible. It shows little appreciation for the wonders that surround it though, instead still clutching to a dirty little bag.  |
| 173 | 1 | A fearful little lizard that clutches a bag of treasure in its tiny hands.\n\nThis variety is known to spend all day reading indoors. However, it seems to mostly enjoy picture books and it's unclear if it can actually read at all. |
| 174 | 1 | A fearful little lizard that clutches a bag of treasure in its tiny hands.\n\nThough it seems as fearful as any other variety of Star Lizard, it was somehow bold enough to secure a luxurious life living in a castle.  |
| 175 | 1 | A fearful little lizard that clutches a bag of treasure in its...\n\nWait no, it has a bomb! By the gods, run! |
| 176 | 1 | A feather adorned blade forged for a gentleman of distinguished tastes. It's owner is known to be something of a bard and most certainly not a bunch of worms in a human skin, don't be ridiculous. \n\nThe mesmerizing gem mounted at it's guard can call forth majestic screaming birds, somehow this seems familiar.  |
| 177 | 1 | A few weeks ago I exchanged books with my dearest friend Inha. I've left the book she lent me over there, its contents made my cheeks quite flushed to be honest. In comparison the book I lent her was quite grim, I hope she doesn't take it the wrong way. If you see her, please let her know I quite enjoyed the book and would love to room with her when this whole mess blows over. |
| 178 | 1 | A fine silvered duck, quite beautiful indeed. As thanks I'll make some new wares available to purchase.  |
| 179 | 1 | A finely crafted bladed rapier used by royal soldiers of the Bamaen army in a war fought long ago. \n\nIt has been lightly enchanted and it can perform the legendary thousand strike sword technique |
| 180 | 1 | A finely crafted dagger belt bestowed upon Prince Beruim Abraon as a coming-of-age gift. \n\nA powerful enchantment causes the daggers carried by this belt to return to their sheaths when thrown.\n\nThe dagger sheaths are adjustable and could likely hold all manner of throwing weapons. |
| 181 | 1 | A finely marbled hunk of meat, seasoned and roasted to perfection. \n\nHigh in protein, this meal helps build strong muscles.  |
| 182 | 1 | A fizzing bottle of bright red liquid extracted from brawler's berries. \n\nBrewed with additional ingredients which create a more potent boost to strength and a slightly improved flavor compared with simply eating the berries.  |
| 183 | 1 | A flawlessly crafter golden duck, etched with ornate designs.\n\nYou have no doubt Umbada will find this enchanting. |
| 184 | 1 | A fleshy imp similar to the gargoyles excavated from beneath the Tower of Knowledge, however it's missing the regular coat of stone.\n\nIt seems to have hidden itself away in the cliffs above Obmuer Temple for quite some time.  |
| 185 | 1 | A floating ball of agony, the gem protruding from its melting flesh emits waves of azure flame. |
| 186 | 1 | A floating ball of despair, the gem protruding from its putrefying flesh emits a powerful curse. |
| 187 | 1 | A flying creature from the Dekanima similar to a manta ray. It flows gracefully through the air and fires beams of powerful energy.\n\nThe Sin Keepers experimented with forbidden knowledge they were sworn to conceal. They paid dearly for this and now the Dekanima spills through into the Record of Sin. |
| 188 | 1 | A flying creature from the Dekanima similar to a manta ray. It flows gracefully through the air and fires beams of weakening energy.\n\nForbidden rituals performed in the castle have weakened the barrier between this world and the Dekanima, allowing it to slip through. |
| 189 | 1 | A flying insect that has grown to tremendous size. \n\nAt this size the usual prey will not suffice, awakening a hunger for human blood. |
| 190 | 1 | A forest-dwelling relative of the otherworldly plants of the Dekanima, its unblinking eye unleashes a torrent of azure flame. |
| 191 | 1 | A formidable mechanical knight plated in blackened copper armor and wielding two sharp hand axes.\n\nConstructed by the Kingdom of Muojwi to fight in a war that ended thousands of years ago. Now it roams the underworld beneath the Isle of Neasied, set to put an end to any trespassers it encounters. |
| 192 | 1 | A formidable mechanical knight plated in pristine steel armor and armed with a huge mechanical lance and spiked round shield.\n\nConstructed by the Kingdom of Muojwi to fight in a war that ended thousands of years ago. Someone has set it to watch over a forbidden key, hidden away atop the Deimpo Mausoleum. |
| 193 | 1 | A fragment of bone said to be from the skeletal remains of Ruunen, avatar of Puwema. \n\nWhile it's certainly quite old, verifying its authenticity is an impossible task.  |
| 194 | 1 | A frail ancient short sword capable of a secret technique few know of.\n\nOnce Pandora's Box has been opened, only a fool would try to close it. |
| 195 | 1 | A friendly pebble from another reality, he once helped adventurers navigate the pale abyss. He's been mounted on a handle for use as a hammer, how cruel!\n\nYou can swing him around to crush your enemies. When his monocle shines, you can even fling him away as a high speed projectile. What ever did he do to deserve such treatment? |
| 196 | 1 | A funny little duck carved from stone. \n\nYou can sense a mercantile aura emanating from within. |
| 197 | 1 | A giant golden gavel forged for King Gius. The king often presided over the royal court, and he purportedly did use this gavel to proclaim his rulings.\n\nIt's socketed with 3 triangular magic gems, which once charged allow the wielder to release magical waves of judgement. |
| 198 | 1 | A gigantic rotting mass of flesh and sharp bones surrounding a gaping maw. It spews waves of disease-ridden bile. |
| 199 | 1 | A gilded snake twists around the striking end of this golden spear. \n\nBlessed by the God Jigeki, the snake lives in an eternal slumber, waking only to unleash a deadly venom when enthralled by combat.  |
| 200 | 1 | A glaucusion that has grown to enormous size through experimentation by the Sin Keepers. It's flesh has become quite raw due to its rapid expansion.\n\nIts bite has become extremely deadly, shredding its prey's flesh as raw as its own. |
| 201 | 1 | A gold Orbewa crown. It was worn by Muagyo before she became the avatar of Jigeki.\n\nIt was found on what seems to be Yubior's grave, perhaps offered in mourning. |
| 202 | 1 | A gold Orbewa necklace. |
| 203 | 1 | A gold Orbewa tiara. |
| 204 | 1 | A gold figurine of a ratty bug, its surface etched with fine detail and inlaid with shining white gems. |
| 205 | 1 | A gold fish shaped treasure, its surface etched with fine detail and its eyes set with shining rubies. |
| 206 | 1 | A gold pendant of House of Abraon's crest. Held by members of the Abraon family to prove their identity when travelling abroad. \n\nIt has been enchanted with a blessing of defense and resistance. |
| 207 | 1 | A gold statuette depicting Queen Siraatsu, the last known avatar of the void goddess Sipeha. |
| 208 | 1 | A gold statuette depicting Ruunen, the avatar of the nature goddess Puwema in an ancient time.  |
| 209 | 1 | A gold statuette depicting Yubior, the avatar of the rotten god Jigeki. |
| 210 | 1 | A gold statuette depicting an unknown avatar of the thunder god Wodaso. |
| 211 | 1 | A gold statuette of a star lizard, its surface etched with fine detail. It's been set with four gleaming rubies.  |
| 212 | 1 | A gold trimmed black hammer fitted with a tremendous crimson orb. Able to cast a most foul spell which absorbs the life force from distant targets.\n\nIt was the favored weapon of Prince Emdaer from the Kingdom of Ommu who lead an army invading Bamaen centuries ago. According to legend, when the war was lost he set this hammer aside and cast himself into the sea. |
| 213 | 1 | A golden bell with a bright orange gem embedded. \n\nIt has an air of magic about it, its surface a bit warmer to the touch than the air surrounding it.  |
| 214 | 1 | A golden ceremonial trident of the Orbewa people. \n\nIt can be used as a weapon, however the head seems to only be loosely affixed to the handle.  |
| 215 | 1 | A golden duck shaped key that can unlock Umbada's signature bright yellow merchandise chests. \n\nHe wouldn't trust this to just anyone but where in the world could those chests be? |
| 216 | 1 | A golden fish, shining like the sun! You will treasure it always yes. |
| 217 | 1 | A golden lamp with a small window of green glass in its center. \n\nThe faint smell of something like dried herbs wafts from within. |
| 218 | 1 | A golden lamp with a small window of pink glass in its center. \n\nThe surface feels a bit electric to the touch and makes your hair stand on end, how strange. |
| 219 | 1 | A golden pendant in the shape of the symbol of goddess Puwema. The holy green gems affixed emit a gentle healing magic. |
| 220 | 1 | A golden stake forged in the shape of the symbol of the god Wodaso.\n\nCrafted for use in symbolic ceremony, it would not make a particularly effective weapon. |
| 221 | 1 | A golden torch blazing with otherworldly azure flames. It can be used to light ceremonial fires, set enemies ablaze, or simply as a light in the dark.  \n\nWhen magic accumulates in the affixed gems, it can hurl a blazing ball of azure fire. |
| 222 | 1 | A guide written by Jayphotic that explains just how he does it. \n\nFollowing the instructions within, things seem a bit tougher than usual. Perhaps this is some form of training.  |
| 223 | 1 | A guitar-like sword forged for "the one of many names" and his band of warriors. What could go wrong with a blade like this in your hands? \n\nIts energy can realign to emit powerful sonic rings, this is not a reference to any type of hog. |
| 224 | 1 | A halberd forged of gold and scales from another reality where humanoid bugs inhabit the world, how unusual and charming!\n\nIt can be used to slash like an axe or thrust like a spear from quite a distance, proving to be a versatile weapon for many situations. |
| 225 | 1 | A hard clay jar, firmly affixed to the end of a stick. \n\nEccentric warriors may find this exactly to their tastes. |
| 226 | 1 | A hard, pungent-tasting fruit that can be found growing deep underground. \n\nEating it raw improves your ability to see in the dark for a little while. \n\nWhile cooking it greatly improves the flavor and texture, the process unfortunately mitigates any useful effects. |
| 227 | 1 | A heavy  sword of polished stone crafted by Epha during the ancient war with the Kingdom of Muojwi.\n\nEach strike releases Epha stone magic and when the gem in its guard is fully charged it can unleash a devastating wide slash. |
| 228 | 1 | A hefty spiked club forged for a truly detestable pride demon. You can almost hear a voice whisper something foul on the wind as you hold it, who said that?\n\nSteals a bit of life with each strike. |
| 229 | 1 | A high-spec automaton drilling unit. It has multiple premium features not found on the standard models. |
| 230 | 1 | A high-spec automaton war machine. It has multiple premium features not found on the standard models. |
| 231 | 1 | A highly aerodynamic and extremely sharp mechanical throwing blade. |
| 232 | 1 | A historical record of the Isle of Neasied. It details the centuries-long construction and expansion of the Deimpo Mausoleum and the Tower of Knowledge. It also recounts the tale of the emergence of Obmuer temple from deep within the sea...\n\nIt's not of particular interest as you've heard these stories a thousand times in your youth.  |
| 233 | 1 | A historical record of the Kingdom of Muojwi's uprising and battle against the Kingdom of Epha. A centuries-long war raged between all the kingdoms in the world, Epha allied with Bamaen and Muojwi allied with Ommu. \n\nWhile you've heard of this war many times, what catches your eye are details about the Muojwi arming themselves with technology from deep underground that had long ago been forbidden. These details are all but erased from records of the war found outside of the Record of Sin.   |
| 234 | 1 | A historical record of the Royal Academy of Bamaen's founding several centuries ago. It contains painfully boring records of headmasters, teachers, fields of study, and ceremonial practices. \n\nHaving studied at the academy in your youth, this is the last thing you want to read about. |
| 235 | 1 | A holy amulet of the Orbewa yes. May it shine bright around your neck like the star. |
| 236 | 1 | A holy looking data module, you should bring to Ossi so he can analyze its contents. |
| 237 | 1 | A horrible mass of mutant flesh wielding scythe-like talons living inside of a treasure chest.\n\nPerhaps it's best to approach all such chests with caution as one can never be too sure what lies in wait inside. |
| 238 | 1 | A horrible mass of mutant flesh, wielding scythe-like talons living inside of an Epha stone chest.\n\nHow many ages has it awaited prey, locked away in such a forgotten place? |
| 239 | 1 | A horrible mass of scarlet-stained flesh wielding scythe like talons living inside of an extravagant treasure chest.\n\nYou've never seen a chest quite like this before and it seems unlikely you'll see one again. How frustrating. |
| 240 | 1 | A huge carafe of rejuvenating elixer, distilled from assorted herbs grown in the Kingdom of Bamaen. \n\nIn high demand for its longevity and consistent effectiveness, in such quantity it's sure to heal just about any wound.. |
| 241 | 1 | A huge mangled corpse covered in bony protrusions from head to toe, the spike erupting from what was once a face so heavy that its neck has snapped and dangles limply.\n\nIt has been bound in decorative wrappings and cursed to watch over a hidden temple of Jigeki for hundreds of years. |
| 242 | 1 | A jade statuette depicting Ruunen, the avatar of the nature goddess Puwema in an ancient time.  |
| 243 | 1 | A jar filled with fragrant dried spices from deep underground. As the scent flows through your nostrils a chill runs down your spine. |
| 244 | 1 | A jar filled with strange dried spices from an ancient land. The contents are of unknown origin and still carry an unfamiliar yet sweet and pungent smell. |
| 245 | 1 | A jet black longhammer decorated with the symbol of Sipeha and fitted with various rings, talons, and gems.\n\nIt can call orbs of living void that rip away all before them. |
| 246 | 1 | A katar "borrowed" from the Idinti warriors of the eastern continent by the Iryupe Raiders. \n\nIt can unleash a ghostly blade by harnessing the power of ancestral Idinti spirits. |
| 247 | 1 | A large automaton war machine that patrols Castle Siraatsu.\n\nSince Queen Siraatsu awakened, she's wasted no time restoring her mechanical army. |
| 248 | 1 | A large barnacle that stretches and exposes its neck to spit at passersby.\n\nPerhaps it does this to keep predators away, no matter the reason it's very rude. |
| 249 | 1 | A large black barnacle that stretches and exposes its neck to spit a noxious murky liquid at anyone entering its territory. |
| 250 | 1 | A large bottle of rejuvenating elixer, distilled from assorted herbs grown in the Kingdom of Bamaen. \n\nIn high demand for its longevity and consistent effectiveness, any serious adventurer should always have a few of these on hand. |
| 251 | 1 | A large bronze pendant, engraved with decorative patterns. The gleaming red gem affixed at its center emits powerful magic of strength. |
| 252 | 1 | A large hunk of steel, engraved with decorative patterns. The bright blue gem affixed at its center emits a strong protective magic. |
| 253 | 1 | A large insect that hatches into and then lives concealed inside of a book.\n\nBook Bugs are the progeny of a smaller ancient insect that made their way into the Tower of Knowledge from deep underground following the excavation beneath the tower. |
| 254 | 1 | A large metal skewer with plump ripe tomato and cob of corn affixed. The heavenly scent of the grilled vegetables is quite rejuvenating. \n\nWielding it requires immense willpower, one slip of the mind and you might lose control and devour the entire thing.  |
| 255 | 1 | A large orda flower bud, picked just before it would've bloomed. \n\nThe gem-like pods on its petals contain a rare compound that prevents magical rot build up on the flower, allowing it to grow and thrive in unique environments.  |
| 256 | 1 | A large, pale manticore that has been sent by Queen Siraatsu to keep watch over one of the sacred Wind Gems.  |
| 257 | 1 | A larger than average Star Lizard coated with glittering metallic scales. \n\nIt always has a Steel Ale in hand, it's a bit of a problem really. |
| 258 | 1 | A larger than average Star Lizard with gleaming golden scales and a bright red frill. \n\nIt has quite a taste for Berserker's Brew. |
| 259 | 1 | A legendary blade of night covered in pitch black barbs, it is awarded to only the most dedicated adventurers.\n\nIts power is unmatched and it has been imbued with magic to protect its wielder from all danger. |
| 260 | 1 | A letter is attached to the small legally distinct mascot figurine, it reads:\n\n"My Dearest Melpert,\n\nI pray this weapon finds you well. I have crafted this stone weapon of vine and shroom for the express purpose of throwing at thine enemies. Regrettably, it does not spin when thrown. Nevertheless, it should serve you well as I am quite certain you are playing the game wrong and also this game sucks. \n\nYours truly and forever,\n\nFreshly Baked Games" |
| 261 | 1 | A living club found in the Dekanima, it's covered with sharp bone spikes.\n\nCapable of unleashing sharp pillars of giant bone from the ground when it becomes enraged. |
| 262 | 1 | A living firearm found in the Dekanima, it observes silently.\n\nIt spits forth vile plasma and exploding stone projectiles. |
| 263 | 1 | A living mushroom man from another reality. Once found growing in a massive spire of stone, now he's been thoroughly impaled on a sharp stick and seems to be quite dead, ready to be roasted to serve as survival food in worse times.\n\nWhen you swing this macabre weapon about, the manroom's corpse produces toxic spores. |
| 264 | 1 | A living spear found in the Dekanima, it steals the strength from those it pierces.\n\nIt can release a wave of strength stealing sludge. |
| 265 | 1 | A living white tree that was enshrined in a strange golden room, its cult of worshippers long passed away. \n\nIn its prime, this cult would consume ripe stone fruits that fell from the guardian's branches and keep the seeds as lucky charms.  |
| 266 | 1 | A long range rifle that fires a jolt of electricity, it can be used to power certain machines. It can also unleash a paralyzing bolt that immobilizes foes. \n\nIt requires energy to operate. |
| 267 | 1 | A long wooden club affixed with the skulls of the original Knowledge Keepers. It can send forth ghostly chattering skulls to hunt its prey. \n\nThis macabre weapon is not befitting of the modern and professional standards of the Knowledge Keepers, so they have locked it away in the small cemetery atop the tower. |
| 268 | 1 | A lumpy little fish, saturated through and through with the Sea King's vile poison. \n\nIt's not clear how or why Dagoom stores these fish in his blowhole, thinking about it too much is a bit unnerving.  |
| 269 | 1 | A magical looking data module, you should bring to Ossi so he can analyze its contents. |
| 270 | 1 | A magical pot given to you by Chef Gagi as thanks for returning many of his Meal Tickets. \n\nOn occasion, a delicious meal emerges from within, it's quite a mystery. |
| 271 | 1 | A magician such as yourself deserves a reward yes. |
| 272 | 2 | A majestic viridian ungulacrita that dwells in and is blessed by the magic of the Verdant Hollow. It casts deadly magic homing darts at its foes.  |
| 273 | 1 | A mangled corpse covered in bony protrusions from head to toe, the spike erupting from what was once a face so heavy that its neck has snapped and dangles limply.\n\nIt shambles mindlessly through the mausoleum, seeking to impale anything in its path. |
| 274 | 1 | A manticore who dabbles in occult arts, he has been sent by Queen Siraatsu to keep anyone from venturing near goddess Puwema's temple in the Verdant Hollow. |
| 275 | 1 | A mask from another reality that protected its wearer from the curse of faces.\n\nWhile one need not wear such a mask here, it can impart thief like skills if you choose to put it on. Increases luck and improves your throwing weapon's return speed. |
| 276 | 1 | A massive hunk of ornately etched black steel attached to a sturdy rod. Quite difficult to wield but able to deal devastating blows. |
| 277 | 1 | A massive hunk of rusted iron attached to a sturdy rod. Quite difficult to wield but able to deal devastating blows. |
| 278 | 1 | A massive silver axe embellished with branches bearing azure buds. It can call forth a wave of azure flame. |
| 279 | 1 | A massive slab of iron with a scaled pattern up its fuller, wielding such a weapon requires great strength. |
| 280 | 1 | A massive slab of rusted iron, wielding such a weapon requires great strength. |
| 281 | 1 | A mechanical device that explodes on impact. Simply press the button on top and toss it.\n\nIt seems to be extremely ancient, luckily it seems well made enough to keep its volatile contents stable. |
| 282 | 1 | A mechanical gargoyle that is sworn to watch over the Record of Sin and neutralize anyone found lacking authorization.\n\nIt's armed with a gun that fires waves of binding magic to subdue trespassers. |
| 283 | 1 | A mechanical gargoyle that is sworn to watch over the Record of Sin and neutralize anyone found lacking authorization.\n\nIt's armed with a long warhammer to bludgeon any who set foot inside. |
| 284 | 1 | A mechanical spider dumped in the underworld beneath the Isle of Neasied after the ancient war with the Muojwi.\n\nCapable of delivering a shocking blast from its spinneret. |
| 285 | 1 | A member of Muagyo's congregation, led by the priestess of Jigeki Irdunuub.\n\nThey keep a watch over the sacred grounds above and within Muagyo's temple and perform rituals of the god Jigeki in her name. These rituals have corrupted the area around the temple with powerful magic and rotten waters. |
| 286 | 1 | A metal plated pyramid adorned with 2 solid wings carved from stone.\n\nHold it high above your head and you'll find yourself whisked away to a Wing Altar.\n\nCarefully constructed ages ago using long forgotten techniques, it can withstand frequent travel.  |
| 287 | 1 | A metal plated skull with twisted horns and unusual gems embedded.\n\nIn an age long past it was used in occult rituals, however any power within it seems to have long ago faded.  |
| 288 | 1 | A mindless homopiscium that wanders the Rotting Cavern spewing waves of noxious swamp gas. |
| 289 | 1 | A miniature replica of Mr Quackers, mascot of Umbada's Trading Post. \n\nIt has some heft to it and could be used as a moderately effective throwing weapon. |
| 290 | 1 | A mirror with a surface so pitch black, nothing but the vaguest hints of reflection appear. |
| 291 | 1 | A mundane looking data module, you should bring to Ossi so he can analyze its contents. |
| 292 | 1 | A mysterious garden of stone, built long ago by visitors from the ancient Kingdom of Epha. |
| 293 | 1 | A mysterious statue of a giant bat, wings held up like it's draping a cape. It has a large sphere of green emerald inlaid in its base. |
| 294 | 1 | A mysterious statue of a homopiscium, it's taken a knee and looks quite serious indeed. It has a large sphere of red ruby inlaid in its base. |
| 295 | 1 | A mysterious statue of a pus bag, mouth agape and quite disgusting. It has a large sphere of blue sapphire inlaid in its base. |
| 296 | 1 | A mysterious statue of a ratty bug, its wings raised in a panic, it almost looks fearsome. It has a large sphere of purple amethyst inlaid in its base. |
| 297 | 1 | A mysterious statue of an ungulacrita, standing quite tall and majestic. It has a large sphere of black onyx inlaid in its base. |
| 298 | 1 | A nautigallus that was imbued with occult magic by Queen Siraatsu. It escaped the castle and made a nest in a pile of ancient scrap. |
| 299 | 1 | A nearly impossible to wield weapon forged from the arm of an ancient golem.\n\nEngraved in the handle is the name "Undercutz" whatever could it mean? |
| 300 | 1 | A necklace worn by Captain Aganmi of the Iryupe Raiders. He gave this to you as thanks for helping him learn the fate of his crew. \n\nThe magic gem affixed provides a small resistance to poison and produces a mild healing aura. Surely a boon during long voyages across the seas. |
| 301 | 1 | A necklace, I take that to mean the worst... well I suppose it's better than not knowing what happened at all. |
| 302 | 1 | A needle like silver rapier, usually carried by elite soldiers. |
| 303 | 1 | A numbered paper ticket that has "Chef Gagi and En's food services" written on it.\n\nIt has pictures of all kinds of food drawn on the front and what appears to be a gigantic crow drawn on the back.  |
| 304 | 1 | A once in a lifetime find, this decorative metal trinket houses a finely polished azure crystal.\n\nSqueeze it tightly in your palm and say a small prayer and you will be whisked away to a Crystal Monument. Unlike the more common brittle fragments, this well crafted piece will not break no matter how tightly it is held.\n\nA priceless gift from the Epha, only the highest ranking knights of Bamaen would have been given to such an item. |
| 305 | 1 | A paralyzing needle used by the Idinti Wanderers to subdue tremendous arachnids that inhabit the eastern continents deserts. |
| 306 | 1 | A pearlescent-shelled ratty bug with a glittering shine. When threatened, it explodes in a blaze of shining glory, decimating all before it.  |
| 307 | 1 | A peculiar ancient cutting tool that fires compressed waves of air.\n\nIt requires energy to operate. |
| 308 | 1 | A pile of hard black stones animated to keep trespassers out of the underworld beneath the Isle of Neasied. |
| 309 | 1 | A polearm that features a powerful sigil at its head combining a needle like tip and crescent blade allowing for versatile attacks. \n\nWhen its gaping red gem is alight with magic, it steals a bit of life essence from foes struck with it. |
| 310 | 1 | A polished stone throwing dagger forged by the ancient Epha. It slows foes to a crawl with Epha stone magic. |
| 311 | 1 | A prince? What are the odds of that, eh? Well you're welcome here all the same, as long as you have coin. |
| 312 | 1 | A putrid glaucusion that dwells in the fetid waters of the Rotting Cavern. The corrupting magic of Jigeki seeps from its pores. |
| 313 | 1 | A quartz statuette depicting an unknown avatar of the thunder god Wodaso. |
| 314 | 1 | A quick charging device for portable electronics. It seems to be compatible with Energy Cells, recharging them slightly faster than usual. |
| 315 | 1 | A rare mineral oil used to protect the surface of weapons from rust and decay.\n\nThe specific ingredients of this highly sought after concoction are a mystery, making it quite difficult to come by.\n\nApplying this oil is a good way to let your weapon know you care about it, increasing the bond you share. |
| 316 | 1 | A rare purple root harvested in autumn. \n\nTraditionally prepared in stews and soups which warm the body and fend off disease. \n\nLess palatable in its raw form but more effective as a medicine. |
| 317 | 1 | A rare ratty bug with gleaming golden scales. \n\nIt's hidden itself away after years of pursuit by treasure hunters.  |
| 318 | 1 | A rare relic from the war between Ommu and Bamaen, this strange blade absorbs the life force from foes it strikes. Press your hand firmly against the spikes on its handle to channel your own blood into the blade, fueling an extremely powerful magical slash. |
| 319 | 1 | A rare species of large ungulacrita, it has a unique bright coloration and stings with a highly toxic poison that weakens its prey.  |
| 320 | 1 | A rather large crow named En. She's been fitted with a saddle so that Gagi can fly about on her back and deliver his mysterious meals all around the isle.  |
| 321 | 1 | A rather large rat-like creature adorned with gleaming red shell and wings.\n\nUsually quite fearful of humans. Something on this isle has driven these creatures mad with rage.  |
| 322 | 1 | A rather large sea-slug that defends itself from predators by slamming into them and excreting a shocking disease-ridden slime. \n\nThe bright color may be attractive but it's best not to touch it.  |
| 323 | 1 | A ratty bug species thought to have been hunted to extinction for its rare blue shell decades ago.\n\nIt sports a large distinctive horn making it substantially more dangerous than its common red counterpart.  |
| 324 | 1 | A record of the life of Peha, avatar of Puwema many thousands of years ago. It describes her life on another world that humanity once called home. She awoke from some kind of long sleep to find its inhabitants gone to the world we live in now.  She faced many automatons covered in human bones, and eventually escaped on some kind of vessel. She arrived on this world and defeated Queen Siraatsu using Puwema's magic and guided humanity in the establishment of its divided kingdoms. \n\nYou find this to be a somewhat dry historical account of entirely fantastical events that seem quite unlikely to  …(truncated) |
| 325 | 1 | A refined powdered tree bark imported from the southern kingdom of Ommu. \n\nThis renowned medicine near instantly relieves stiff joints and loosens tight and cramped muscles.  |
| 326 | 1 | A relic from a war fought long ago, thoroughly rusted yet retains a sharp edge. \n\nIt still feels quite solid despite its apparent age.  |
| 327 | 1 | A relic from an ancient war, this mechanical knight still functions despite a thick layer of flora growing from its every crack and crevice.\n\nUmbada must have repaired it to stand guard over his shop. |
| 328 | 1 | A repeating crossbow that carries the faint scent of hot dogs and ramen, how odd. |
| 329 | 1 | A replica of the Holy Sword Unok, sister of Holy Sword Endu. Though wielding it is similar in feel, it isn't capable of quite the same magical attacks as the real thing.\n\nThe original was destroyed when King Gius struck it against Queen Siraatsu's inhuman breast. |
| 330 | 1 | A rotten fish that swims in the fetid waters of the Rotting Cavern. |
| 331 | 1 | A royal looking data module, you should bring to Ossi so he can analyze its contents. |
| 332 | 1 | A rusted automaton knight from an ancient army. Though it hasn't seen combat for ages, its combat routines are still intact. |
| 333 | 1 | A rusted automaton soldier from a long-forgotten regime. It still contains the order: kill on sight. |
| 334 | 1 | A rusted automaton used for drilling and repair work in the ancient world. It has been reprogrammed to kill all intruders. |
| 335 | 1 | A rusted security drone that patrols in a never-ending search for trespassers. It's been programmed to fire on sight. |
| 336 | 1 | A rusted war machine that was decommissioned millennia ago. It has been reactivated with a single order: keep intruders out of the generator tower. |
| 337 | 1 | A scorched black skull, ablaze with magic flame. Summoned by Husipo to bind her enemies. |
| 338 | 1 | A scythe used in ancient death reaping rituals. Capable of hurling disease-ridden spinning shadow clone blades. |
| 339 | 1 | A security drone that patrols Castle Siraatsu. It will fire on intruders without hesitation.\n\nSince Queen Siraatsu awakened, she's wasted no time restoring her mechanical army. |
| 340 | 1 | A semi-transparent fish surging with otherworldly azure flame. Long ago it swam deep below the lake it resides in and encountered something most people, let alone fish, ever witness. |
| 341 | 1 | A sepiicanis that was imbued with occult magic by Queen Siraatsu. It escaped the castle and hid itself away in an abandoned lab. |
| 342 | 1 | A serf that has grown to near immeasurable size. It devours anything that sets foot in the befouled waters of the Rotting Cavern.\n\nWhen Okmaru ventured into the water the gigantic serf devoured her whole, it's a good thing the key wasn't dissolved by its stomach acid. |
| 343 | 1 | A series of first-hand accounts about the isolated Kingdom of Ritudeon who dominate the western continent. They are a relatively small and tribal kingdom, known to have very different practices and traditions compared to most of the world. The western continent is widely covered in dense jungles, within which many unique plants and animals can be found. \n\nOf particular note are a multitude of plants that have curative properties and a rare species of tree which produces an extremely hard and uniquely blue tinted wood, all commodities which are prized the world over. |
| 344 | 1 | A shambling mass of decaying flesh that wanders aimlessly through the waters of the Rotting Cavern. |
| 345 | 1 | A shambling mass of flesh summoned by the Necronomicon's Necrotic Bael flesh form.\n\nIt knows only pain and hunger. |
| 346 | 1 | A shattered zweihander that was repaired by filling the cracks with enchanted gold.\n\nWhen enough magic accumulates in the cracks it can unleash a paralyzing magic slash. |
| 347 | 1 | A siege weapon from a war fought long ago, the rusted metal head of the hammer is affixed to a creaky wooden pole. Slow to wield but has good reach.  |
| 348 | 1 | A siege weapon from a war fought long ago, the silver plated head of the hammer is affixed to a solid wooden pole. Slow to wield but has good reach.  |
| 349 | 1 | A silver bell with a bright blue gem embedded. \n\nIt has an air of magic about it, its surface a bit cooler to the touch than the air surrounding it.  |
| 350 | 1 | A silver duck shaped key that unlocks the door to Umbada's Trading Outpost.  |
| 351 | 1 | A silver figurine of a ratty bug, its surface etched with fine detail and inlaid with bright colorful gems. |
| 352 | 1 | A silver fish shaped treasure, its surface etched with fine detail and its eyes set with bright cerulean gems. |
| 353 | 1 | A silver pendant of House of Abraon's crest. Imparted to knights who perform exceptional deeds in service to the kingdom. \n\nIt has been enchanted with a blessing for strength and vitality. |
| 354 | 1 | A silver statuette depicting Queen Siraatsu, the last known avatar of the void goddess Sipeha. |
| 355 | 1 | A silver statuette depicting Ruunen, the avatar of the nature goddess Puwema in an ancient time.  |
| 356 | 1 | A silver statuette depicting Yubior, the avatar of the rotten god Jigeki. |
| 357 | 1 | A silver statuette depicting an unknown avatar of the thunder god Wodaso. |
| 358 | 1 | A silver statuette of a star lizard, its surface etched with fine detail. It's been set with four cerulean gems.  |
| 359 | 1 | A silver wreathed eye found growing in the Dekanima.\n\nFires a shredding beam of otherworldly energy. |
| 360 | 1 | A simple brass key dropped by the drilling unit found on The Tower of Knowledge rooftop. \n\nIt must fit the lock of the nearby chest which doubtless contains an extravagant treasure of some kind.  |
| 361 | 1 | A simple silver bangle that has been enchanted with a spell of quick return. It's able to enhance the enchantment on your dagger belt, hastening the return of weapons thrown from its sheaths.  |
| 362 | 1 | A sizable bird, defeathered and roasted to perfection. \n\nThe crisp golden skin and moist succulent meat nourish the body.  |
| 363 | 1 | A skull plated in silver and etched with elaborate designs.\n\nYou can hear faint voices echo from somewhere inside. |
| 364 | 1 | A slick black spear wrapped in stained and shameful rags.\n\nAble to produce powerful magical waves of a mysterious liquid, though it will leave it's wielder quite sticky. Perhaps this is what the rags are for.  |
| 365 | 1 | A small bottle of liquid that enhances ink generation of your grimoire's writing quill, allowing for more frequent inscriptions. \n\nJust think of all the strange things you'll be able to write now! |
| 366 | 1 | A small brittle fragment of gleaming azure crystal. \n\nCrush it in your palm while uttering a small prayer and you may find yourself at a Crystal Monument.\n\nCrystal Monuments were constructed all across Bamaen by the Epha during the ancient war with the Kingdom of Muojwi. The crystals emit a healing aura as well as ward off evil, making them the ideal spot for weary warriors to rest after battle. |
| 367 | 1 | A small communication device that flashes the words "service unavailable" on its screen.  |
| 368 | 1 | A small fragment of stone, carved with a blessing of protection. Often worn by those on pilgrimage to help bear the hardships. |
| 369 | 1 | A small golden pendant featuring the image of a cat. |
| 370 | 1 | A small metal charm affixed with a black tourmaline gemstone. Its aura speeds the expulsion of disease from the body.  |
| 371 | 1 | A small metal charm affixed with a green agate gemstone. Its aura cleanses binding magic from the body.  |
| 372 | 1 | A small metal charm affixed with a large block of white quartz. Its aura cleanses heaviness from the body.  |
| 373 | 1 | A small metal charm affixed with a purple amethyst gemstone. Its aura speeds the expulsion of poisons from the body.  |
| 374 | 1 | A small metal charm affixed with a red garnet gemstone. Its aura speeds the expulsion of weakening toxins from the body.  |
| 375 | 1 | A small metal charm affixed with a white opal gemstone. Its aura helps cleanse sticky substances from the body.  |
| 376 | 1 | A small sack sewn from rough spun fabric carried by Aquatic Star Lizards. \n\nBy its weight you judge it to contain a small metal bauble of some kind. It smells terribly rancid, you don't have the stomach to open it yourself.  |
| 377 | 1 | A small sack sewn from rough spun fabric carried by Deep Star Lizards. \n\nBy its weight you judge it to contain a small metal bauble of some kind. There's a thick coat of slime covering it, you don't have the stomach to open it yourself.  |
| 378 | 1 | A small sack sewn from rough spun fabric carried by Gleaming Star Lizards. \n\nBy its weight you judge it to contain a small metal bauble of some kind. A dark magical energy emanates from within, it would be foolish to open it yourself.  |
| 379 | 1 | A small sack sewn from rough spun fabric carried by Grave Star Lizards. \n\nBy its weight you judge it to contain a small metal bauble of some kind. It's dripping with grave wax, you don't have the stomach to open it yourself.  |
| 380 | 1 | A small sack sewn from rough spun fabric carried by Learned Star Lizards. \n\nBy its weight you judge it to contain a small metal bauble of some kind. A thick coat of mold covers it, you don't have the stomach to open it yourself.  |
| 381 | 1 | A small sack sewn from rough spun fabric carried by Regal Star Lizards. \n\nBy its weight you judge it to contain a small metal bauble of some kind. It seems to be quite clean compared to similar loot sacks and yet, you don't feel like opening it yourself as it's tied quite tightly. |
| 382 | 1 | A small sack sewn from rough spun fabric carried by Star Lizards. \n\nBy its weight you judge it to contain a small metal bauble of some kind. It's absolutely filthy, you don't have the stomach to open it yourself.  |
| 383 | 1 | A small scale stone model of an ancient city, excavated from beneath The Tower of Knowledge.\n\nSuch a strange and marvelous sight, no city with such grand walls as the model implies is ever known to have existed. \n\nFurther research is being undertaken by the Royal Academy of Bamaen to determine if such a city truly existed at one point in history. |
| 384 | 1 | A small silver pendant featuring the image of a cat. |
| 385 | 1 | A small stuffed toy made in the image of a ratty bug. \n\nApparently everybody wants one of these but you aren't entirely sure why.  |
| 386 | 1 | A small vial of rejuvenating elixer, distilled from assorted herbs grown in the Kingdom of Bamaen. \n\nIn high demand for its longevity and consistent effectiveness, this small vial contains just enough for basic treatment. |
| 387 | 1 | A small wooden charm, carved with a blessing of vitality. Its effect is minimal, however it carries a faint sweet smell which is quite pleasing.  |
| 388 | 1 | A spear forged long ago in the Kingdom of Ommu. It has been imbued with an occult enchantment allowing it to produce a magical copy of itself when thrown. |
| 389 | 1 | A species of Meganeura that lives in the Rotting Cavern.\n\nThese foul creatures spew a weakening toxin from their behinds. How disgusting! |
| 390 | 1 | A species of Meganeura that lives in tropical climates. \n\nUnlike the more common type, it has a much harder, gleaming emerald shell and carries a slowing toxin in its stinger. |
| 391 | 1 | A stalker summoned from the Dekanima through the Josami Blood Angel ritual pool.\n\nThis creature's presence seems to be a side effect of the unholy ceremonies held within the chamber. |
| 392 | 1 | A standard issue blade for soldiers from the long forgotten ancient past. Energy can be run through its blade to deliver a paralyzing slash. |
| 393 | 1 | A standard morning star, used to bash and pierce armored opponents in battle. |
| 394 | 1 | A stone hammer, the head of which is shaped like the mascot of Umbada's Trading outpost. It seems to be extremely old.\n\nIt emits a magical aura of purity, once charged it can heal nearly any ailment. |
| 395 | 1 | A stone imp excavated from the caverns beneath the Tower of Knowledge. It wears a monocle and seems not to share the violent inclinations of its brethren.\n\nWourha believed she caught a glimpse of a gargoyle reading a book once. However, when she glanced away and back, it was as still as solid stone and she assumed she must have imagined it.  |
| 396 | 1 | A stone imp excavated from the caverns beneath the Tower of Knowledge. While many lay dormant, this one has come to life and armed itself with a golden crossbow.\n\nWhat causes one to come alive and another to remain dormant remains a mystery. Perhaps it's just a matter of time. |
| 397 | 1 | A stone imp excavated from the caverns beneath the Tower of Knowledge. While many lay dormant, this one has come to life and armed itself with a golden halberd.\n\nThese creatures showed no signs of life when originally dug up, leading to their distribution across the Tower of Knowledge as decoration.  |
| 398 | 1 | A strange cybernetically enhanced servant of Queen Siraatsu. It impales any who oppose the queen with its filthy metallic beak. |
| 399 | 1 | A strange cybernetically enhanced servant of Queen Siraatsu. She fires beams of binding magic from the massive wheel embedded in her torso. |
| 400 | 1 | A strange device that aids in physical training when affixed to the body.  |
| 401 | 1 | A strange fruit which grows in coastal regions during the winter.\n\nIts shell is quite difficult to crack open but well worth the effort. \n\nThe sweet flesh releases an acidic compound that temporarily hardens the skin when consumed. |
| 402 | 1 | A strange looking data module, you should bring to Ossi so he can analyze its contents. |
| 403 | 1 | A strange mechanical man covered with bones.\n\nUnder its artificial breath it mutters "Peha..." |
| 404 | 1 | A strange, dog-like cephalopod that adapted to living on land as the waters of the ancient world became tainted by industrial run-off.\n\nIt bounds joyfully about while secreting bubbles of debilitating grease as it goes.  |
| 405 | 1 | A strangely shaped fossilized crystal, you can see just a hint of blue shimmering through the calcified surface. \n\nThe energy flowing from it is useful if you have to go fast. |
| 406 | 1 | A sturdy leather fist covering embedded with large metal studs. \n\nA fine weapon for those who want to get up close and personal. |
| 407 | 1 | A sturdy silver ceremonial trident of the Orbewa people. \n\nAlthough it's meant for ceremony, it can be used as a reliable weapon.  |
| 408 | 1 | A sword forged from interdimensional metal from the Dekanima. \n\nThe hidden god Okbujo's magic blesses this sword, allowing its wielder to pierce the veil and directly confront and banish the gods after slaying their avatars.\n\nOnly those who have offered sufficiently powerful souls to Okbujo's shrine are allowed to possess it.  |
| 409 | 1 | A symbol of the rot god Jigeki, wrought from iron and adorned with beads. \n\nInscribed with a partially legible prayer, it seems it was used in an unknown ritual of worship in some forgotten age.  |
| 410 | 1 | A tauropod summoned from the Dekanima through the Josami Blood Angel ritual pool.\n\nThis creature's presence seems to be a side effect of the unholy ceremonies held within the chamber. |
| 411 | 1 | A thick and dusty volume detailing various processes to achieve the final stage of creating the great work. \n\nWhile it's clear that the goal is a "reddening" it is unclear what parts of this text are literal and which are metaphorical.  |
| 412 | 1 | A throwing knife imbued with occult magic that guides it towards its target. |
| 413 | 1 | A traditional hunter's weapon of the Idinti Wanderers. Its blades can unleash a deadly weakening toxin.\n\nThis weapon was passed down to Wourha by her mother who travelled to Bamaen from the eastern continent before Wourha's birth.  |
| 414 | 1 | A tremendous bronze morning star, patina lines its intricately etched head. Certainly not the average armament on any battlefield. |
| 415 | 1 | A tremendous high tech gauntlet that can fire long range rockets and perform a powerful rocket punch.\n\nIt requires energy to operate. |
| 416 | 1 | A tremendous human wearing a suit of unidentifiable origin. Ossi discovered this explorer floating in the black waters below his lab. \n\nYou helped Ossi collect the data modules he needed to research this abomination and ultimately shock it back to life. It was a regrettable course of action upon reflection. |
| 417 | 1 | A tremendous mechanical lance constructed in the Kingdom of Muojwi in ancient times. It was intended to be wielded by mechanical knights and is quite laborious to thrust.\n\nA trigger on the handle can activate a drilling function, sending the wielder hurtling forward as the segments of the lance spin at high speed. In the war between Muojwi and Epha, mechanical knights would use this function to drill tunnels beneath the ground useful as hard to detect strategic paths for invading armies. |
| 418 | 1 | A tremendous rusted morning star. Certainly not the average armament on any battlefield. |
| 419 | 1 | A tremendous spear created by the Sin Keepers. It uses forbidden magic to transport its blade to faraway targets. |
| 420 | 1 | A trident shaped key with a black onyx embedded on its face. \n\nSymbolic of the oceans, this standardized key was crafted by the Orbewa people. |
| 421 | 1 | A trident shaped key with a blue sapphire embedded on its face. \n\nSymbolic of the oceans, this standardized key was crafted by the Orbewa people. |
| 422 | 1 | A trident shaped key with a green emerald embedded on its face. \n\nSymbolic of the oceans, this standardized key was crafted by the Orbewa people. |
| 423 | 1 | A trident shaped key with a purple amethyst embedded on its face. \n\nSymbolic of the oceans, this standardized key was crafted by the Orbewa people. |
| 424 | 1 | A trident shaped key with a red ruby embedded on its face. \n\nSymbolic of the oceans, this standardized key was crafted by the Orbewa people. |
| 425 | 1 | A trident shaped key, hollow of its former magic.\n\nCan still function in mechanical locks. |
| 426 | 1 | A truly heretical book claiming that the beings we call gods are not forces of nature at all. It describes them as spirits of beings from long dead worlds beyond our own. \n\nYou cannot bear to read further, surely this is blasphemy. |
| 427 | 1 | A two pronged throwing blade imbued with life stealing blood magic. |
| 428 | 1 | A versatile high tech fox tail scythe that can flip around and function as a hammer. \n\nWhen enough energy from both modes has accumulated, it can unleash a devastating azure flame attack from the scythe or a crushing heavy attack from the hammer. |
| 429 | 1 | A very brief book about the northern continent known as Udyu. It is nearly impossible to access due to high cliffs surrounding the entire land mass that drop directly to the sea. The book claims that the continent is entirely uninhabited, though it seems to mostly be speculative.  |
| 430 | 1 | A very unusual golden pus bag. It wears an extravagant crown atop it's malformed and melting skull.\n\nIt does seem quite regal in its own way, perhaps it truly was a king before it became this way. |
| 431 | 1 | A very unusual metallic pus bag. It wears a silver crown atop it's malformed and melting skull.\n\nIt believes itself to be a king but a king of what is unclear. |
| 432 | 1 | A vial of acid that can dissolve growths of soft tissue spawned by the Necronomicon.\n\nIronically, the recipe for this acid was found within the Necronomicon's pages and the vial itself coughed up from the vile book's own mouth. \n\nYou have applied it to your throwing daggers. |
| 433 | 1 | A vibrant green zweihander with a nice little rat carved above it's guard. \n\nBrings a little bit of magic and luck, just like its namesake. |
| 434 | 1 | A warhammer forged long ago in the Kingdom of Ommu. It has been imbued with an occult enchantment allowing it to produce a magical copy of itself when thrown. |
| 435 | 1 | A well balanced throwing axe. |
| 436 | 1 | A wheel dancer appointed as a duchess by Queen Siraatsu, she patiently waits for the wheel to spin in her favor. \n\nIt seems Obetda was entertaining her in their chamber, just what exactly did you interrupt? |
| 437 | 1 | A winged copper key with a diamond shaped emblem. Unlocks the gate to the shrine housing Holy Sword Endu, the legendary flying sword of House Abraon. |
| 438 | 1 | A/V Travel |
| 439 | 1 | Account Manager |
| 440 | 1 | Acid |
| 441 | 1 | Acid Vial |
| 442 | 1 | Activate every Crystal Monument and Wing Altar. |
| 443 | 1 | Add To Quick Slots |
| 444 | 1 | Additional sound effects |
| 445 | 1 | Aegis Wing |
| 446 | 1 | After centuries of peace, the forces of evil stir yet again in the Kingdom of Bamaen. |
| 447 | 1 | Aganmi's Camp |
| 448 | 1 | Aganmi's Memento |
| 449 | 1 | Aganmi's Necklace |
| 450 | 1 | Agate Link Charm |
| 451 | 1 | Agate Thorny Gape |
| 452 | 1 | Ageless guardians of the Deimpo Mausoleum, this enchanted pile of stones comes to life to prevent trespass.  |
| 453 | 1 | Ah, there are schematics here for upgrading weapons of a quite modern variety. These aren't likely to be anything terribly fancy but perhaps they suit your tastes. |
| 454 | 1 | Aha! A human prince? All was foretold yes. The wheel of fortune spins and brings you right around to me.  |
| 455 | 1 | Aha! There are schematics here for upgrading high-tech weapons. I don't expect you understand the difference between these devices and magic, do you? |
| 456 | 1 | Aha! You've been hard at work yes. Gathering them up, fast as the chariot.  |
| 457 | 1 | Ahh, I've been expecting you, Prince Beruim. I'd been rudely awakened by her call and she's asked me to guide you a bit, however, I am quite tired.  |
| 458 | 1 | Ahoy there! It's good to see a friendly face. I'm Captain Aganmi of the Iryupe Raiders. My crew has gone missing since we washed up on these shores and I'm hoping you could be of some assistance in locating them. You are friendly, right? Now that I get a good look at you, I fear you might be... no... |
| 459 | 1 | Akumu Amulet |
| 460 | 1 | Alas, Prince Beruim has failed just as his father did. He must seek the true holy sword if he is to prevail... |
| 461 | 1 | Albino Bookbug |
| 462 | 1 | All books must be returned to the library before the rare artifact storage will be reopened and that is final! \n\nPlease return the missing books to the following sections:\n\nI - Alchemic Recipes\n\nII - Fantastical Creatures\n\nIII - Construction Techniques\n\nIV - Fairy Tales\n\nV - Eldritch Horror\n\nVI - Queer Romance |
| 463 | 1 | All day and all night I cook the food, deliver the food. A never ending cycle of gourmet delight! Why you ask? I've had a vision you see, that some young hero would venture across this isle and bring divine justice upon that wretched queen! Yet this hero cannot succeed alone, nourishment is required! |
| 464 | 1 | All unsaved progress will be lost.\n\nYou have never saved! |
| 465 | 1 | All unsaved progress will be lost.\n\nYour last save was {Time} ago |
| 466 | 1 | Also, I'm not going anywhere any time soon, may as well give you this too. |
| 467 | 1 | Always Hide |
| 468 | 1 | Always Show |
| 469 | 1 | Amethyst Tear Charm |
| 470 | 1 | Amulet of the White Branch |
| 471 | 1 | Amulet worn by cultists of the White Tree. A small gem sits floating fixed in the center. It has been blessed with the essence of the white tree in order to improve the wearer's luck and prosperity. |
| 472 | 1 | An Onyx statuette depicting Queen Siraatsu, the last known avatar of the void goddess Sipeha. |
| 473 | 1 | An SVT 38 customized with a charming ribbon, radiation charm, and a decal of the adorable mascot Pyp.\n\nIt's ten round clip can be reloaded any number of times, just where do the bullets come from? |
| 474 | 1 | An absolutely huge syringe wielded like a thrusting weapon by Eyepea the dream weaver. \n\nIt sucks dream energy from foes it pierces and once filled up, it can fire a nightmare needle projectile. |
| 475 | 1 | An advanced device that synchronizes the user with their main weapon, making it quicker to use more effectively. |
| 476 | 1 | An aggressive, slithering creature with massive horns and a hard bone shell. It raged freely through the Dekanima, ripping anything in its path to shreds.\n\nThe Sin Keepers experimented with forbidden knowledge they were sworn to conceal. They paid dearly for this and now the Dekanima spills through into the Record of Sin.  |
| 477 | 1 | An amulet from another reality, it bears image of Akumu who dwelled within dreams in the dying Kingdom of Mortis.\n\nAkumu's enchantment invigorates the accumulation of energies, magical and otherwise |
| 478 | 1 | An amulet that contains the image of an adorable little dog. \n\nIt bestows a small blessing that invigorates you, helping you make it through even the most difficult of journeys. |
| 479 | 1 | An ancient amulet of the Orbewa people, it still shines quite brightly with holy magic. It carries a small blessing of resistance as well as wards off magic rot accumulation.  |
| 480 | 1 | An ancient brass automaton used for drilling and repair work. The Knowledge Keepers seem to have used it for maintenence of the tower. |
| 481 | 1 | An ancient claw weapon forged from diamond-hard black metal. |
| 482 | 1 | An ancient combat rifle, equipped with a small missile launcher. \n\nIt requires energy to operate. |
| 483 | 1 | An ancient cracked mirror made of solid metal, you can just barely make out your reflection in its fragmented surface.\n\nLight refracted by its surface takes on a strange hue and reveals details that you could swear were not there before. |
| 484 | 1 | An ancient fish with a powerful bite, usually known to hide in the depths.\n\nThis species is far older and a bit more vicious than those found on the surface. |
| 485 | 1 | An ancient fish with a powerful bite, usually known to hide in the depths.\n\nWhether due to turbulent waters or the influence of dark magic, they have recently surfaced to attack anyone within reach of their strong jaws. |
| 486 | 1 | An ancient green bottle filled with a fermented liquid. The worn out label reads "good taste forever - blended style".\n\nThe date is in a format that you don't recognize, when and where is this from? |
| 487 | 1 | An ancient leaf shaped blade, once wielded by highly skilled warrior priests of Puwema.\n\nDespite the thick coat of rust it can still be used to fight elegantly. |
| 488 | 1 | An ancient leaf shaped blade, once wielded by highly skilled warrior priests of Puwema.\n\nIt has been restored and polished to a bright shining finish. |
| 489 | 1 | An ancient looking data module, you should bring to Ossi so he can analyze its contents. |
| 490 | 1 | An ancient portable device that can hold energy used by a variety of strange weapons. \n\nYou've never seen this type of magic before. |
| 491 | 1 | An ancient red bottle filled with a fermented liquid. The worn out label reads "good taste forever - deep red".\n\nThe date is in a format that you don't recognize, when and where is this from? |
| 492 | 1 | An ancient stone hammer crafted in the Kingdom of Epha. It was gifted to the Orbewa as a gift during the war with the Kingdom of Muojwi.\n\nWhen the great cerulean orb in the center of its head accumulates enough energy it can unleash the power of the sea. |
| 493 | 1 | An ancient tome bound in human skin and containing the most evil of black magic incantations, rituals, and sorceries.\n\nIt was found during excavations beneath the Tower of Knowledge and locked away in the forbidden archives on the upper levels. Upon Queen Siraatsu's revival it sprung to life and tricked the knight Budaen into reading from its pages, setting it free from the bindings placed upon it by the Knowledge Keepers. |
| 494 | 1 | An ancient tome which describes the complete domination of the world by mechanical men adorned with human bones.\n\nYou've never heard of a single place described in this book. It seems to be a very inventive work of fiction even though it has been written in the style of a historical record. |
| 495 | 1 | An ancient yellow bottle filled with a fermented liquid. The worn out label reads "good taste forever - sparkling white".\n\nThe date is in a format that you don't recognize, when and where is this from? |
| 496 | 1 | An ancient, moss-covered golem that was napping peacefully for hundreds of years until you came knocking.\n\nIts body is formed from a uniquely sturdy but incredibly heavy stone, so you've kept the arm to wield as a club. |
| 497 | 1 | An automaton knight that patrols Castle Siraatsu, it wields a deadly plasma lance and shield.\n\nSince Queen Siraatsu awakened, she's wasted no time restoring her mechanical army. |
| 498 | 1 | An automaton soldier that patrols Castle Siraatsu. It's equipped with an advanced plasma sword.\n\nSince Queen Siraatsu awakened, she's wasted no time restoring her mechanical army. |
| 499 | 1 | An average Orbewa citizen, it fights to keep all trespassers out of the Obmuer Temple.\n\nWhen the Sea King was ensorcelled by Muagyo, the Orbewa broke an ancient pact and turned against the Kingdom of Abraon. |
| 500 | 1 | An elaborate metal charm affixed with speckled orange and purple jasper gemstones. Its aura slows the accumulation of magic rot in the body.  |
| 501 | 1 | An elite Josami knight, she is a crazed fighter who wields a shredding mechanical mace. \n\nThe Occult Order of Josami have been deployed by Queen Siraatsu to keep watch on the underworld beneath the Isle of Neasied. |
| 502 | 1 | An elite Josami knight, she is a tactical fighter who wields a spear and a large magical shield.\n\nThe Occult Order of Josami have been deployed by Queen Siraatsu to keep watch on the underworld beneath the Isle of Neasied. |
| 503 | 1 | An engraved halberd used by soldiers of the Kingdom of Ommu during a war fought long ago. \n\nIt can be used to slash like an axe or thrust like a spear from quite a distance, proving to be a versatile weapon for many situations. |
| 504 | 1 | An enormous decaying bat, it's been coated with magic rot. It must have ventured into the Rotting Cavern. |
| 505 | 1 | An esoteric diamond shaped blade forged in an age long past. \n\nIts true purpose long forgotten, it can still serve as a versatile weapon in the right hands. |
| 506 | 1 | An ethereal Orbewa Fetus, given to you by the spirit of Erma, Matron of the Deep. \n\nEven now, her spirit weeps over the destroyed Orbewa eggs. |
| 507 | 1 | An ethereal chef's hat, given to you by the spirit of Chef Gagi. |
| 508 | 1 | An ethereal crest bearing the royal seal of the Kingdom of Bamaen, given to you by the spirit of Knight Captain Ekonta. |
| 509 | 1 | An ethereal crown of the King of Bamaen, given to you by the spirit of your father King Gius Abraon. |
| 510 | 1 | An ethereal hourglass, given to you by the spirit of Knowledge Keeper Wourha. |
| 511 | 1 | An ethereal mechanical arm, given to you by the spirit of Mechanical Engineer Ossi. |
| 512 | 1 | An ethereal necklace of the Iryupe Raiders, given to you by the spirit of Captain Aganmi. |
| 513 | 1 | An ethereal symbol of Goddess Puwema, given to you by the spirit of Peha. |
| 514 | 1 | An evil looking data module, you should bring to Ossi so he can analyze its contents. |
| 515 | 1 | An evil queen who ruled this world millennia ago, once banished to an eternal slumber. Since her re-awakening she has begun to raise armies of malevolent monsters and machines in preparation for a war of conquest.\n\nShe is the human avatar of Sipeha, goddess of the void. Her near eternal life is widely attributed to this, and yet, other avatars of the goddess have come and gone. Perhaps there is more to her story?\n\nYou must succeed where your father failed and put an end to her before she can rule again. |
| 516 | 1 | An exceptionally large Book Bug that hatched within an oversized tome.  |
| 517 | 1 | An exquisitely crafted golden hourglass inlaid with bright red rubies and pouring a fine diamond sand. \n\nThe sand has been enchanted with magic that greatly hastens all forms of improvement. Wourha kept this close at hand while completing her research.   |
| 518 | 1 | An extravagant spiked buckler once used by soldiers from Ommu. It offers minimal defense and was mostly used as an offensive weapon.\n\nIt can be used as a main weapon by striking using the large spike on the front. Carefully timed parry attacks can be followed up with a powerful strike as well. |
| 519 | 1 | An extremely advanced ancient spear with a blade of pure plasma energy. \n\nIt requires energy to operate. |
| 520 | 1 | An extremely advanced ancient sword with a blade of pure plasma energy. \n\nIt requires energy to operate. |
| 521 | 1 | An extremely advanced ancient throwing knife with a blade of pure plasma energy. It requires energy to operate. |
| 522 | 1 | An extremely boring book about the construction of various barriers that block or control the flow of water. |
| 523 | 1 | An extremely pale book bug that found its way into a tome in Siraatsu's castle. It's all on its own so it tries to remain hidden away. |
| 524 | 1 | An illustrated book featuring some quite scintillating imagery and very inventive stories.\n\nIn this volume the heroine Kotaro Kuko faces off against the villainous vampiress Bebo Danado.\n\nJust before the battle, Bebo says her iconic line "Ah, you're coming in my direction?" Everyone knows this line, even if they haven't read the story.  |
| 525 | 1 | An impaler appointed as a duke by Queen Siraatsu, he eagerly awaits the day he rules over a part of the world above. \n\nIn the meantime, he whiles away the days with his beloved Ubhaer in their chamber in the castle. |
| 526 | 1 | An iron pendant, painted with a sigil of strength. Many warriors keep such a pendant close to heart at all times. |
| 527 | 1 | An old tarnished mirror used by mystics to scry. \n\nStaring into its surface doesn't seem to present you with any visions beyond your own face, maybe you just don't know how to use it. |
| 528 | 1 | An ornate brass pocket watch with pink crystal embellishments. \n\nIt seems to be in disrepair as the hands are moving backwards. |
| 529 | 1 | An ornate knife used in burial ceremonies, it has been tainted with disease.  |
| 530 | 1 | An ornate silver pocket watch with pink crystal embellishments. \n\nIt seems to be in disrepair as the hands are moving far too fast. |
| 531 | 1 | An ornate wooden panel. You feel as though you've seen it before. Many, many times before, in other worlds, other lives only half remembered. A chill runs down your spine and you feel that you must look away. |
| 532 | 1 | An ornately crafted compass mounted inside of a finely carved wooden box.\n\nThe hard blue wood is from the western continent where the Kingdom of Ritudeon lies deep with the jungle. Ritudeon explorers are usually given such a compass before setting off into the greater world. |
| 533 | 1 | An ornately crafted compass mounted inside of a finely carved wooden box.\n\nWhile it would be quite useful for navigating the seas, it seems the Iryupe Raiders intended to sell it as they had kept it stored away in a loot chest. |
| 534 | 1 | An ornately crafted golden mask that covers the top half of the face. \n\nWearing it allows you to see and dispel the magic of the four gods when affixed with the proper lenses. |
| 535 | 1 | An otherworldly creature that stalks the darkest halls of the Deimpo Mausoleum. \n\nIt quite grievously injured the knights that accompanied King Abraon into these ancient depths. |
| 536 | 1 | An otherworldly throwing blade of extremely sharp bone soaked in weakening toxins.  |
| 537 | 1 | An otherworldly throwing dagger with twisting blue prongs holding an unblinking eye. It sets its target ablaze with azure flames of the Dekanima. |
| 538 | 1 | An over-engineered and impractical weapon, it's unclear what madness drove its creation. The feathers are certainly quite gay though. <img id="prideflag_ico"></> |
| 539 | 1 | An unhatched egg of the Orbewa people. Inside the transparent shell, a small fetus can be seen pulsing gently. \n\nIt must be returned to an Orbewa Matron if it's ever to be hatched. |
| 540 | 1 | An unnaturally large bat found in the depths of the Deimpo Mausoleum. Best not to imagine what it was eating to reach such a size.  |
| 541 | 1 | An unnaturally large bat that lives in the underworld beneath the Isle of Neasied. It has a unique dappled pattern on its coat and skin. |
| 542 | 1 | An unnaturally large spider found in the Deimpo Mausoleum. These spiders seem quite successful at spinning webs to catch all manner of prey. |
| 543 | 1 | An unusual flower that grows deep underground, adapted to absorb energies other than sunlight. \n\nConsume it to cleanse magical bindings from the body.  |
| 544 | 1 | An unusually shaped one of a kind stone block that can be used as a throwing weapon. It's quite heavy. |
| 545 | 1 | An unusually shaped stone block that can be used as a throwing weapon. It's quite heavy and bears the marks of Epha stone magic. |
| 546 | 1 | Analog Controls |
| 547 | 1 | Ancient Aegis Wing |
| 548 | 1 | Ancient Besom Cutter |
| 549 | 1 | Ancient Coin |
| 550 | 1 | Ancient Data Module |
| 551 | 1 | Ancient Drilling Unit |
| 552 | 1 | Ancient Game |
| 553 | 1 | Ancient Green Bottle |
| 554 | 1 | Ancient Mecha Knight |
| 555 | 1 | Ancient Pail Chopper |
| 556 | 1 | Ancient Prisoner's Note |
| 557 | 1 | Ancient Red Bottle |
| 558 | 1 | Ancient Short Sword |
| 559 | 1 | Ancient Stone Duck Figurine |
| 560 | 1 | Ancient Technology |
| 561 | 1 | Ancient World |
| 562 | 1 | Ancient Yellow Bottle |
| 563 | 1 | Ancient stone tower where the Knowledge Keepers house a massive library of tomes. |
| 564 | 1 | Ancient tower built deep within the ground, it houses forbidden knowledge and artifacts. |
| 565 | 1 | Angel |
| 566 | 1 | Anima Mundi Blessing |
| 567 | 1 | Another Home - Peha's Journey |
| 568 | 1 | Another world below the surface, littered with forgotten ancient technology. |
| 569 | 1 | Apply |
| 570 | 1 | Apply Resolution and Window Mode |
| 571 | 1 | Apply Video Changes? |
| 572 | 1 | Aquatic Star Lizard |
| 573 | 1 | Arch Sepiicanis |
| 574 | 1 | Archivist Inha had exchanged books with her dear friend Wourha but to her surprise, the book Wourha lent her was quite grim. \n\nFeeling that this was perhaps a message from Wourha that her interest in rooming together was in fact, not mutual at all, Inha read aloud a dark incantation from within the book despite Wourha's warning not to do so.\n\nThis proved a fatal mistake and she was dissolved down to nothing but bones. Oops. |
| 575 | 1 | Archivist Inha's Journal I |
| 576 | 1 | Archivist Inha's Journal II |
| 577 | 1 | Archivist Inha's Journal III |
| 578 | 1 | Are you sure you wish to delete\n\nall progress on slot {Index}? |
| 579 | 1 | Are you.. the prince of the Abraon family? What in the world are you doing here? Since that knight arrived things have gone from bad to worse. I should never have trusted such a foolish man with the key to the forbidden archives... |
| 580 | 1 | Arjaot |
| 581 | 1 | Artifacts of Epha |
| 582 | 1 | As Muagyo's power grew, she bestowed upon her congregation great magics of Jigeki. The devoted congregation was sealed inside the inner sanctum to perform sacred rituals.\n\nSecond of the shrines was in lands far below, hidden away near the bridge to Queen Siraatsu's magnificent castle. |
| 583 | 1 | Aspect Ratio |
| 584 | 1 | At a glance it appears to be a large succulent fowl. Closer inspection reveals hard chitinous plates and antennae.\n\nThese creatures could be quite formidable if underestimated. |
| 585 | 1 | Attack Power |
| 586 | 1 | Attack Power Up |
| 587 | 1 | Attack Speed |
| 588 | 1 | Attack Speed Up |
| 589 | 1 | Attacking with Energy Weapons consumes Cell Energy. Cell Energy is shared between all Energy Weapons and recharges slowly over time.\n\nYou can find additional Energy Cells to increase the number of successive attacks that are possible with Energy Weapons. |
| 590 | 1 | Attempting to give fire to one who does not wish it will surely invite trouble.\n\nGargoyle V and both of his neighbors are good friends, they are all in agreeance about the taking of fire. |
| 591 | 1 | Audio |
| 592 | 1 | Audio Options |
| 593 | 1 | Auger Impaler |
| 594 | 1 | Aureate Ratty Bug |
| 595 | 1 | Auto |
| 596 | 1 | Auto Add Items To Quick Slots |
| 597 | 1 | Auto Put Away Items |
| 598 | 1 | Average |
| 599 | 1 | Away with you! I need to experiment with the data from these modules in peace! |
| 600 | 1 | Axe-wielding Josami warrior, she fights like she's got nothing to lose.\n\nThe Order of the Josami are sworn to protect Queen Siraatsu and her castle. |
| 601 | 1 | Azure Crawler |
| 602 | 1 | Azure Flame |
| 603 | 1 | Azure Torch |
| 604 | 1 | B |
| 605 | 3 | B33 Punch |
| 606 | 1 | BW |
| 607 | 1 | Back |
| 608 | 1 | Backspace |
| 609 | 1 | Badu Powder |
| 610 | 1 | Baked Lobster |
| 611 | 1 | Baleful Chilopoda |
| 612 | 1 | Banish |
| 613 | 1 | Barnacle |
| 614 | 1 | Before Okbujo's Tree |
| 615 | 1 | Before that creature attacked us, Nu took off into the corridors below with the key to this supply chest in hand, damn that coward. Even so, I pray he still lives, perhaps you'll find him below and we can make use of the supplies in this chest to make our escape. |
| 616 | 1 | Beneath Rotten Waters |
| 617 | 1 | Berserker's Brew |
| 618 | 1 | Beruim! My dearest son! How in the world did you make it here? Alas, Unok was shattered upon Siraatsu's inhuman breast, my faith must have been too weak to wield her properly. That baneful Queen tossed me aside as if I were nothing but a meager toy doll.  |
| 619 | 1 | Beryl Chromobula |
| 620 | 1 | Besom Cutter |
| 621 | 1 | Besom Cutter Pro |
| 622 | 1 | Bestiary |
| 623 | 1 | Bestiary Updated |
| 624 | 1 | Beyond the upper floors there you will find access to a bridge which leads to the mausoleum. I pray the mess I've made isn't enough to halt your progress. You may yet find his Majesty. \n\nPuwema and Wodaso's blessing upon you, young prince. |
| 625 | 1 | Binding |
| 626 | 1 | Binding Res |
| 627 | 1 | Binding yourself with this finely crafted rope prevents the absorption of the contents of your stomach. |
| 628 | 1 | Binding yourself with this finely crafted rope prevents the accumulation of unwanted experience. |
| 629 | 1 | Binding yourself with this finely crafted rope prevents the bond with your weapons from increasing. |
| 630 | 1 | Bindings |
| 631 | 1 | Bipoum the Raging |
| 632 | 1 | Bird Flail |
| 633 | 1 | Birds of a Feather |
| 634 | 1 | Black Diamond Claw |
| 635 | 1 | Black Skeletal Butcher |
| 636 | 1 | Black Skeletal Gunner |
| 637 | 1 | Black Skeletal Reaper |
| 638 | 1 | Black Skeletal Witch Husipo |
| 639 | 1 | Black Steel Greataxe |
| 640 | 1 | Black steel blade coated in a thick poisonous residue.  |
| 641 | 1 | Blade Bathed in Holy Light |
| 642 | 1 | Blast it! I revived that thing and this is how I'm repaid? |
| 643 | 1 | Blood Angel Berserker |
| 644 | 1 | Blood Angel Josami Key |
| 645 | 1 | Blood Angel Lord Woyeki |
| 646 | 1 | Blood Angel Lord Yuumed |
| 647 | 1 | Blood Angel Lords Yuumed & Woyeki |
| 648 | 1 | Blood Angel Pendant |
| 649 | 1 | Blood Angel Sentinel  |
| 650 | 1 | Blood Angel Sisterhood |
| 651 | 1 | Blood Angel Templar |
| 652 | 1 | Blood Angel Warden |
| 653 | 1 | Blood Key Stone |
| 654 | 1 | Blood Moon |
| 655 | 1 | Blue |
| 656 | 1 | BoBladely |
| 657 | 1 | Boingggggg... upppp youuuu gooooo... |
| 658 | 1 | Bone Blade |
| 659 | 1 | Bone Machine |
| 660 | 1 | Bone Machines |
| 661 | 1 | Bones of Archivist Inha |
| 662 | 1 | Bones of a powerful warrior who guarded the Shrine of Endu centuries ago, now reanimated by black magic. \n\nStill adorned with strong plated armor and wielding the massive cleaver entrusted to shrine guardians. \n\nIn its clouded thoughts a voice whispers, "let none defile the shrine." |
| 663 | 1 | Bones of a powerful warrior who guards a long-forgotten tomb beneath the cemetery beside Deimpo Mausoleum. \n\nIt has been lying in wait to stop any intrusion into this tomb for centuries, wielding an evil twisted sword and wearing a thorny brass crown atop its head.\n\nWhy does it guard the tomb so fiercely? |
| 664 | 1 | Book Bug |
| 665 | 1 | Book Bugs hatched into forbidden tomes in the Record of Sin, they wield the azure flame of the Dekanima. |
| 666 | 1 | Book Bugs hatched into magical tomes seem to gain some occult knowledge, perhaps they read some of the contents while growing inside. |
| 667 | 1 | Borderless |
| 668 | 1 | Bound Spiker |
| 669 | 1 | Bound in human skin, it tried to eat me. I narrowly escaped and it flew off towards the top of the tower, cackling madly. What is that book? How could such a thing be possible?  |
| 670 | 1 | Brass Drilling Unit |
| 671 | 1 | Brass Key |
| 672 | 1 | Brass Pocket Watch |
| 673 | 1 | Brawler's Berries |
| 674 | 1 | Bright red little fish with razor sharp teeth, known to easily strip prey down to the very bone. \n\nThey never hunt alone so keep your eyes open, at least it shouldn't be hard to spot them. |
| 675 | 1 | Brightness |
| 676 | 2 | Brilliant red lobster baked and doused in butter. \n\nTruly fine dining, eating it enhances the spirit.  |
| 677 | 1 | Bring Sixty Meal Tickets to Chef Gagi. |
| 678 | 1 | Bring azure flame to my brethren who dwell upon the great beacons across the isle. \n\nThey will send fire to the braziers high atop the beacons and signal the ringing of the colossal bell of The Tower of Knowledge. \n\nWhen all three beacons are alight and the bell has rung thrice, The Tower of Knowledge will be unsealed.  |
| 679 | 1 | Bring both holy swords and twelve sacred gems. |
| 680 | 1 | Bring every Orbewa Egg to Erma, Matron of the Deep. |
| 681 | 1 | Bring every duck figurine to Umbada. |
| 682 | 1 | Broiled Meat |
| 683 | 1 | Bronze Great Morning Star |
| 684 | 1 | Brutal Amulet |
| 685 | 1 | Budaen |
| 686 | 1 | Buried in the files are schematics for upgrading magical weapons. Quite interesting, I can't make heads or tails of the science behind this but no matter. |
| 687 | 1 | Burn |
| 688 | 1 | By socketing the Storm Gem into Holy Sword Endu, a new ability can be used that charges slowly through combat. \n\nOnce the Storm Gem on the upper left of the HUD is fully charged you can unleash the Holy Storm Ultra Attack by pressing {specialattack_binding}. |
| 689 | 1 | By the gods, Piodor was right! Not only have we found a grave that seems likely to contain the remains of Yubior, we've found a statue of an Orbewa Woman bearing the symbol of Jigeki. The man we hired did not... make it here with us. Upon setting out in the boat, he screamed and flung himself overboard taking an oar with him. Rowing here with just one oar was no small feat I must say. In the time we've spent studying the statue and grave here it seems the water has lowered too far to take the boat back. Certainly it's just the tides and we'll be able to set out again within a day. |
| 690 | 1 | C |
| 691 | 1 | CAW CAW CAW! |
| 692 | 1 | CAW CAWWWW! |
| 693 | 1 | CAWWWWW! |
| 694 | 1 | Can you finish Queen's Domain using nothing but spells? \n\nThis is probably the only spell in the game, I wouldn't try it. |
| 695 | 1 | Cancel |
| 696 | 1 | Candle Crawler |
| 697 | 1 | Cannot return to Crystal Monument right now. |
| 698 | 1 | Cannot travel right now. |
| 699 | 1 | Caps Lock |
| 700 | 1 | Captain of the Occult Order of Josami, she keeps watch over Puwema's temple with her noble dragon Wujomiod. \n\nNone may enter the temple, for the power of the shrine within could bring a final end to Siraatsu's reign. |
| 701 | 1 | Captured Man's Note |
| 702 | 1 | Careful with that key, boy. You'll not survive a meeting with our mothers. |
| 703 | 1 | Carrot |
| 704 | 1 | Castle Lords Obetda and Ubhaer |
| 705 | 1 | Castle Servant's Journal |
| 706 | 1 | Causin' much mayhem, droppin' drama\n\ngot a nail-bat and I mean to harm ya\n\nThe 3D virtual emerald embedded in this bat can unleash a chaotic 32-bit polygonal slam attack. |
| 707 | 1 | Cave Entrance |
| 708 | 1 | Cave scorpion carefully prepared and served up on a skewer, crunchy and a bit sharp.\n\nThe venom of this scorpion has a blood clotting effect, after proper preparation the effect is mellowed.\n\nConsuming it counteracts weakening toxins in the blood. |
| 709 | 1 | Cell Cost: |
| 710 | 1 | Cell Phone |
| 711 | 1 | Cemetery Precipice |
| 712 | 1 | Ceremonial Stone Tiara |
| 713 | 1 | Cestus |
| 714 | 1 | Chakram |
| 715 | 1 | Chakram from the Kingdom of Ommu, used in the ancient war with the Kingdom of Bamaen.\n\nIt's been precisely constructed and balanced to change directions mid-flight and return after travelling a short distance. |
| 716 | 1 | Challenge Complete |
| 717 | 1 | Challenges |
| 718 | 1 | Champion Keeper |
| 719 | 1 | Charcoal grilled white fish, seasoned with a bit of salt and pepper.\n\nRich in oils that help you stay healthy and sharpen your vision.  |
| 720 | 1 | Charming Jar |
| 721 | 1 | Chattering Skull |
| 722 | 1 | Check out Blinge on youtube! |
| 723 | 1 | Check out Boba on twitch! |
| 724 | 1 | Check out Byrd on twitch! |
| 725 | 1 | Check out Caput Mortum on steam! |
| 726 | 1 | Check out Cerb on twitch! |
| 727 | 1 | Check out Detestable on twitch! |
| 728 | 1 | Check out Devil Spire on steam! |
| 729 | 1 | Check out Dreamspring on steam! |
| 730 | 1 | Check out Dungeon Chill on youtube! |
| 731 | 1 | Check out FlyKnight on steam! |
| 732 | 1 | Check out Ghostie on twitch! |
| 733 | 1 | Check out Gil's games on steam! |
| 734 | 1 | Check out Hag on twitch! |
| 735 | 1 | Check out Hark The Ghoul on steam! |
| 736 | 1 | Check out Harpo on twitch! |
| 737 | 1 | Check out Hempie on twitch! |
| 738 | 1 | Check out Jay on twitch! |
| 739 | 1 | Check out Luke's games on steam! |
| 740 | 1 | Check out Luna on twitch! |
| 741 | 1 | Check out Pale Abyss on steam! |
| 742 | 1 | Check out Pea on twitch! |
| 743 | 1 | Check out Ratboy on youtube! |
| 744 | 1 | Check out Red Vox on their official site! |
| 745 | 1 | Check out Roco on twitch! |
| 746 | 1 | Check out Sao on twitch! |
| 747 | 1 | Check out Tini on twitch! |
| 748 | 1 | Check out Tris's games on steam! |
| 749 | 1 | Check out Verho on steam! |
| 750 | 1 | Check out Vinny on twitch! |
| 751 | 1 | Check out b33 on twitch! |
| 752 | 1 | Chef Gagi |
| 753 | 1 | Chicket |
| 754 | 1 | Chill Amulet |
| 755 | 1 | Chilopoda |
| 756 | 1 | Chinese |
| 757 | 1 | Citrine Jigeki Statuette |
| 758 | 1 | Clams that have been steamed and drenched in a rich butter and herb sauce.\n\nPacked with nutrients that help you feel spry and energized.  |
| 759 | 1 | Clean up the Tower of Knowledge. |
| 760 | 1 | Cleansing Flower |
| 761 | 1 | Clear Binding |
| 762 | 1 | Cliff Edge |
| 763 | 1 | Clone Boy |
| 764 | 1 | Close |
| 765 | 1 | Cobalt Blue Seal Breaker |
| 766 | 1 | Coelacuda |
| 767 | 1 | Collect Okbujo's Judgement. |
| 768 | 1 | Collect all four lenses for the Mask of Second Sight. |
| 769 | 1 | Collect every Accessory. |
| 770 | 1 | Collect every Main Weapon. |
| 771 | 1 | Collect every Treasure. |
| 772 | 1 | Collect every type of Throwing Weapon. |
| 773 | 1 | Collect four Energy Cells. |
| 774 | 1 | Collect the Holy Sword Endu. |
| 775 | 1 | Come closer, prince, I'll suck the life from you, like I did that foolish knight. |
| 776 | 1 | Come to grovel before me?\n\nNo? Prepare to die you weak sack of flesh! |
| 777 | 1 | Commander Ekonta |
| 778 | 1 | Common Data Module |
| 779 | 1 | Common Spear |
| 780 | 1 | Complete a mysterious ritual. |
| 781 | 1 | Completion Rate |
| 782 | 1 | Concealed Passages |
| 783 | 1 | Concerned Researcher's Note |
| 784 | 1 | Configure |
| 785 | 1 | Confirm |
| 786 | 1 | Confirm Video Changes? |
| 787 | 1 | Confused Castle Servant's Note |
| 788 | 1 | Conservator's Note |
| 789 | 1 | Continue |
| 790 | 1 | Contrast |
| 791 | 1 | Control Options |
| 792 | 1 | Control Options / Bindings |
| 793 | 1 | Controller Disconnected.\n\nPress {confirm_button} to continue. |
| 794 | 2 | Controls |
| 795 | 1 | Cosmic Bones |
| 796 | 1 | Crawler |
| 797 | 1 | Creatures of ancient legend make their return. |
| 798 | 1 | Credits |
| 799 | 1 | Crimson Grotto |
| 800 | 1 | Crosshair |
| 801 | 1 | Crown of the Sea King |
| 802 | 1 | Crowned ruler of the Orbewa people for the last seven decades. He has recently led the Orbewa in rebellion against the Kingdom of Bamaen.\n\nHe was convinced by Muagyo, priestess of Jigeki to break the generations-long pact with the Kingdom of Bamaen. Muagyo claimed that she was to become the next avatar of the god Jigeki. With this new power, she would assist Dagoom in leading the Orbewa to their rightful place, ruling over all peoples of the world. |
| 803 | 1 | Crumbling Fortress |
| 804 | 1 | Crumbling Stairwell |
| 805 | 1 | Crunch |
| 806 | 1 | Crushing Lance |
| 807 | 1 | Crystal Monuments |
| 808 | 1 | Cudgel of Silence |
| 809 | 1 | Cultic Book Bug |
| 810 | 1 | Current Location |
| 811 | 1 | Cursed Loot Sack |
| 812 | 1 | Cursed Skull |
| 813 | 1 | Curses, curses, curses! I was on my way back to the Tower of Knowledge with this replacement book when of all things, the bridge collapsed! I had the book delivered to the Deimpo mausoleum in secret by those knights from the mainland, thinking I could avoid punishment for losing the original. I believe I have bigger concerns now, I can't muster the strength to stand and my leg seems quite broken. Perhaps someone will come to investigate the bridge's collapse by morning, for now I need to get some rest. |
| 814 | 1 | Cyan |
| 815 | 1 | D |
| 816 | 1 | D-Pad Down |
| 817 | 1 | D-Pad Left |
| 818 | 1 | D-Pad Right |
| 819 | 1 | D-Pad Up |
| 820 | 1 | Dagger Belt |
| 821 | 1 | Dagoom the Sea King |
| 822 | 1 | Daji the Grasping |
| 823 | 1 | Damage Res |
| 824 | 1 | Damage Types |
| 825 | 1 | Damn me! Never should've come here. Thank you for tracking down my crew, may Wodaso guide them to the next life. |
| 826 | 1 | Dams and Weirs |
| 827 | 1 | Daon |
| 828 | 1 | Daon of the Knights of House Abraon sits motionless. He's alive but seems unable to speak. You gently jostle him and he produces a faint, nearly inaudible moan.  |
| 829 | 1 | Dappled Giant Bat |
| 830 | 1 | Dark Data Module |
| 831 | 1 | Dark Silence |
| 832 | 1 | Dark Triangular Slate |
| 833 | 1 | Data Charm |
| 834 | 1 | Dear sister Muagyo, may you find peace. |
| 835 | 1 | Decaying Great Tree |
| 836 | 1 | Deep Mire |
| 837 | 1 | Deep Star Lizard |
| 838 | 1 | Default |
| 839 | 1 | Defeat Blood Angel Lords Yuumed and Woyeki. |
| 840 | 1 | Defeat Dagoom the Sea King. |
| 841 | 1 | Defeat Herald of Okbujo. |
| 842 | 1 | Defeat Josami Lord Saijha. |
| 843 | 1 | Defeat Poenir and Duenir, Malformed Demonic Aberrations. |
| 844 | 1 | Defeat The Necronomicon. |
| 845 | 1 | Defeat Usunha and Wujomiod, Shrine of Puwema Gatekeepers. |
| 846 | 1 | Defeat the Gigantic Serf. |
| 847 | 1 | Defeat this foe to see its information. |
| 848 | 1 | Defenses |
| 849 | 1 | Deimpo Mausoleum |
| 850 | 1 | Deimpo Mausoleum Monument Engraving |
| 851 | 1 | Dekanima Ritual Blood |
| 852 | 1 | Delete |
| 853 | 1 | Delete Slot |
| 854 | 1 | Demonic Crystal Skull |
| 855 | 1 | Demonic Ritual Skull |
| 856 | 1 | Demonic Servitude |
| 857 | 1 | Dense forest filling a massive cave deep beneath the Isle, bathed in light cast by sacred crystals. |
| 858 | 1 | Depraved Mass of Charred Flesh |
| 859 | 1 | Descendant of the Orbewa homopiscium that did not migrate to the surface with the rest of their people. Unlike its ancestors, it cannot manipulate the complex ancient technology surrounding it.\n\nIt has a distinct vermillion colored coat of scales and spits a powerful weakening toxin, both adaptations evolved in such a strange environment. |
| 860 | 1 | Desperate Man's Journal |
| 861 | 1 | Detestable Demon's Kanabo |
| 862 | 1 | Diamond Blade |
| 863 | 1 | Different weapons have a variety of damage types. The effectiveness of each damage type depends on the resistances of the enemy you're hitting. \n\nWeak - corresponding damage type will deal increased damage.\n\nNormal - corresponding damage type will deal normal damage.\n\nResist - corresponding damage type will deal reduced damage. |
| 864 | 1 | Disable Screen Shake |
| 865 | 1 | Discord Moderation |
| 866 | 1 | Disease |
| 867 | 1 | Disease Res |
| 868 | 1 | Dispelling the Epha magic encasing the Strange Stone Block revealed a mechanical cube forged by the Muojwi millennia ago. It's empowered by the magic of the god Wodaso, how strange. \n\nWhen thrown, Wodaso's lightning ripples out in an eviscerating wave.  |
| 869 | 1 | Distinguished Monocle |
| 870 | 1 | Docile Gargoyle |
| 871 | 1 | Dolomite Golem |
| 872 | 1 | Down |
| 873 | 1 | Down Button |
| 874 | 1 | Down Directional Button |
| 875 | 1 | Dragon Lord Usunha |
| 876 | 1 | Dread Silver |
| 877 | 1 | Dream Weaver's Colossal Syringe |
| 878 | 1 | Dried Herbs |
| 879 | 1 | Drilling Unit |
| 880 | 1 | Drilling Unit X |
| 881 | 1 | Drops |
| 882 | 1 | Duchess Ubhaer |
| 883 | 1 | Duenir is a gargantuan demonic spider conjured by Queen Siraatsu herself, left to guard the burial chamber after Sipeha was foolishly awakened by King Gius Abraon. \n\nHe has waited in agony, slowly going insane for millennia in the underworld. Now that the queen has released him, he will stop at nothing to prevent any would-be followers of the queen from entering the depths below. If any should make it past him and Poenir, a return to the underworld surely awaits them.  |
| 884 | 1 | Duenir, Depraved Horns of Agony |
| 885 | 1 | Duke Obetda |
| 886 | 1 | Dusty |
| 887 | 1 | E |
| 888 | 1 | EXP |
| 889 | 1 | EXP to Next |
| 890 | 1 | Eat |
| 891 | 2 | Eclectic Accoutrements |
| 892 | 1 | Effects Volume |
| 893 | 1 | Egg Charm |
| 894 | 1 | Ekonta's Memento |
| 895 | 1 | Elegant Compass |
| 896 | 1 | Elixer |
| 897 | 1 | Emptied Coffers |
| 898 | 1 | Empty |
| 899 | 1 | Empty File |
| 900 | 1 | En, The Giant Crow |
| 901 | 1 | Enable Gamepad |
| 902 | 1 | Enable Steam Screenshots |
| 903 | 1 | Enable Texture Streaming |
| 904 | 1 | Enchanting Jar |
| 905 | 1 | End |
| 906 | 1 | Endless Pages |
| 907 | 1 | Endu Shrine Gate Key |
| 908 | 1 | Enemy Life Bar |
| 909 | 1 | Energy Cell |
| 910 | 1 | Energy Cells |
| 911 | 1 | English |
| 912 | 1 | Engraved Halberd |
| 913 | 1 | Enraged Queen Siraatsu |
| 914 | 1 | Enter |
| 915 | 1 | Epha Dagger |
| 916 | 1 | Epha Mimic |
| 917 | 1 | Epha Sword |
| 918 | 1 | Equip |
| 919 | 1 | Equip Accessory |
| 920 | 1 | Equip Throwing Weapon |
| 921 | 1 | Equip Throwing Weapon 1 |
| 922 | 1 | Equip Throwing Weapon 2 |
| 923 | 1 | Equip Throwing Weapon 3 |
| 924 | 1 | Equip Weapon |
| 925 | 1 | Equip Weapon 1 |
| 926 | 1 | Equip Weapon 2 |
| 927 | 1 | Equip Weapon 3 |
| 928 | 1 | Equipment |
| 929 | 1 | Erda is one of three members of the Royal Orbewa Guard, sworn to protect Dagoom the Sea King.\n\nHe bears a silver nautilus crown, given to him by the Sea King during the Royal Orbewa Guard ceremony. The blessings of this ceremony permanently altered his coloration and bestowed him with ancient Orbewa water magic.  |
| 930 | 1 | Erda of the Royal Orbewa Guard |
| 931 | 1 | Erma's Hatchery |
| 932 | 1 | Erma's Memento |
| 933 | 1 | Erma's Note |
| 934 | 1 | Erma, Matron of the Deep |
| 935 | 1 | Escape |
| 936 | 1 | Eve |
| 937 | 1 | Even in the afterlife, I'll never touch a book again! |
| 938 | 1 | Excavator's Note I |
| 939 | 1 | Excavator's Note II |
| 940 | 1 | Excavator's Note III |
| 941 | 1 | Excellenttttt... wonnnnderfullll... |
| 942 | 1 | Exit |
| 943 | 1 | Exit Shop |
| 944 | 1 | Exit Upgrade Terminal |
| 945 | 1 | Explore every treasure vault. |
| 946 | 1 | Explorer's Cudgel |
| 947 | 1 | Extensive Armaments |
| 948 | 1 | Extravagant Hourglass |
| 949 | 1 | Extremely Slow |
| 950 | 1 | Extremely thin needles that can pierce several targets at once when thrown. Forged by the finest blacksmith in Bamaen long ago.  |
| 951 | 1 | Eye of Silver |
| 952 | 1 | F |
| 953 | 1 | Fallen Angels |
| 954 | 1 | Fallen Raider's Necklace |
| 955 | 1 | Familiar Location |
| 956 | 1 | Familiar of the Blood Angel Sisterhood, they summon these creatures from the Dekanima ritual pit through secret ritual. |
| 957 | 1 | Fanatical Star Lizard |
| 958 | 1 | Fast |
| 959 | 1 | Fate of the Iryupe Raiders |
| 960 | 1 | Fated Wheel Dancer |
| 961 | 1 | Father oh Father Wodaso watch over me. Father of light take away this darkness...\n\nCarry my soul to a higher plane. Guide me oh great father...   |
| 962 | 1 | Feathered Hooked Falx |
| 963 | 1 | Feel the Wind |
| 964 | 1 | Fetid Meganeura |
| 965 | 1 | Field of View |
| 966 | 1 | File Saved |
| 967 | 1 | File Select |
| 968 | 1 | Filthy Loot Sack |
| 969 | 1 | Find a very different type of main weapon. |
| 970 | 1 | Find your father, take up the quest in his stead if you must. |
| 971 | 1 | Fine Loot Sack |
| 972 | 1 | Fine collllectingggg... yessss... |
| 973 | 1 | Fleeing Sin Keeper's Note |
| 974 | 1 | Flesh Gargoyle |
| 975 | 1 | Flying Mushroom Fossil |
| 976 | 1 | Flying Sword |
| 977 | 1 | For all my scrying and divination, I did not foresee this betrayal yes. |
| 978 | 1 | Forbidden Archives |
| 979 | 1 | Forbidden Archives Key |
| 980 | 1 | Forest Eye |
| 981 | 1 | Forest Walker |
| 982 | 1 | Forge the True Holy Sword Akunra. |
| 983 | 1 | Forged long ago by the ancestors of the Abraon family, this sword has spent countless generations slumbering on the Isle of Neasied.\n\nThe guard bears sockets for five Wind Gems and the Storm Gem, but all have been removed and enshrined elsewhere save for a single Wind Gem. \n\nBy channeling the Wind Gems through this sword one can perform Wind Dashes - thrusts of the blade that send its wielder flying through the air. |
| 984 | 1 | Forgive usssss... we have been here so longggggg. Purgatoryyyyy... |
| 985 | 1 | Forgotten Dungeon |
| 986 | 1 | Forgotten Rooms |
| 987 | 1 | Forgotten Sentinel |
| 988 | 1 | Fortune Lamp |
| 989 | 1 | Fossilized Anemone |
| 990 | 1 | Fossilized Fish |
| 991 | 1 | Foyer |
| 992 | 2 | Framerate Cap |
| 993 | 1 | French |
| 994 | 1 | Freshly Dried Herbs |
| 995 | 1 | From a Wing Altar you can travel to any other Wing Altars you've activated.\n\nWhen you find a Wing Altar make sure to step onto the platform to make it available for travel. |
| 996 | 1 | Fullscreen |
| 997 | 1 | Furniture inhabited by inhuman spirits that were released by The Necronomicon.\n\nThe spirits lie in wait and then fling the furniture they possess at passersby. |
| 998 | 1 | Furtive Nautigallus |
| 999 | 1 | G |
| 1000 | 1 | GAME OVER |
| 1001 | 1 | Gagi's Magic Pot |
| 1002 | 1 | Gagi's Memento |
| 1003 | 1 | Gamepad Bindings |
| 1004 | 1 | Gamepad Style |
| 1005 | 1 | Gameplay |
| 1006 | 1 | Gameplay Options |
| 1007 | 1 | Gamer Chair |
| 1008 | 1 | Gargoyle Arbelist |
| 1009 | 1 | Gargoyle Warrior |
| 1010 | 1 | Gargoyle's Hint I |
| 1011 | 1 | Gargoyle's Hint II |
| 1012 | 1 | Gargoyle's Hint III |
| 1013 | 1 | Gargoyle's Tablet |
| 1014 | 1 | Garnet Oval Charm |
| 1015 | 1 | Gem Curse |
| 1016 | 1 | Genesis |
| 1017 | 1 | German |
| 1018 | 1 | Get off the counter! I work hard to keep it clean, you know. |
| 1019 | 1 | Ghost Coelacanth |
| 1020 | 1 | Ghost Manticore |
| 1021 | 1 | Ghost Spear |
| 1022 | 1 | Giant Bat |
| 1023 | 1 | Giant Spider |
| 1024 | 1 | Giant eye flesh form conjured by the Necronomicon, it wreaks destruction wherever its gaze falls. |
| 1025 | 1 | Gigantic Serf |
| 1026 | 1 | Gilded Leaf Blade |
| 1027 | 1 | Gilded Pus Bag |
| 1028 | 1 | Ginger Pendant |
| 1029 | 1 | Gius's Judgement |
| 1030 | 1 | Gius's Memento |
| 1031 | 1 | Gius's Reach |
| 1032 | 1 | Glaucusion |
| 1033 | 1 | Gleaming Star Lizard |
| 1034 | 1 | Glimmering Treasures |
| 1035 | 1 | Gluttonous flesh form conjured by the Necronomicon, it casts demonic spells and calls forth the armies of the damned. |
| 1036 | 1 | Go out past the barricade and search the isle, look for the other knights will you? …and watch your back, there are all manner of beasts roaming about, mad with rage and lusting for blood. |
| 1037 | 1 | Goat-like flesh form conjured by the Necronomicon, it wields a multitude of unholy weapons. |
| 1038 | 1 | God Slayer |
| 1039 | 1 | Gold Conch Necklace |
| 1040 | 1 | Gold Crown |
| 1041 | 1 | Gold Fish Treasure |
| 1042 | 1 | Gold Jigeki Statuette |
| 1043 | 1 | Gold Nautilus Crown |
| 1044 | 1 | Gold Puwema Statuette |
| 1045 | 1 | Gold Ratty Bug Figurine |
| 1046 | 1 | Gold Scallop Tiara |
| 1047 | 1 | Gold Sipeha Statuette |
| 1048 | 1 | Gold Star Lizard Statuette |
| 1049 | 1 | Gold Wodaso Statuette |
| 1050 | 1 | Golden Bell of Fire |
| 1051 | 1 | Golden Crab |
| 1052 | 1 | Golden Duck Figurine |
| 1053 | 1 | Golden throwing dagger once used by King Gius Abraon. \n\nHeavier and more powerful than common throwing daggers.  |
| 1054 | 1 | Golem |
| 1055 | 1 | Golem Arm Crusher |
| 1056 | 1 | Good at thissss you arrrrre... young princcccce... |
| 1057 | 1 | Goodness, you've cleaned up that horrible mess! I could hardly think with all that squelching and moaning. You have my thanks, your Highness. |
| 1058 | 1 | Grand Book Bug |
| 1059 | 1 | Grand Elixer |
| 1060 | 1 | Grave Eye |
| 1061 | 1 | Grave Guardian |
| 1062 | 1 | Grave Knife |
| 1063 | 1 | Grave Reaper |
| 1064 | 1 | Grave Star Lizard |
| 1065 | 1 | Great gods Puwema and Wodaso and even Jigeki and Sipeha! Anyone, any god! Please save me!\n\nThe wheel woman is not at all what I'd hoped. She is quite terrifying and she derives great pleasure from spinning her wretched wheel upon me for hours upon hours. \n\nI fear I may not survive another wheeling but she will not let me leave. |
| 1066 | 1 | Greatsword of the Josami knights, loyal protectors of Queen Siraatsu. It is extremely heavy, you can barely lift it. |
| 1067 | 1 | Green |
| 1068 | 1 | Grenade |
| 1069 | 1 | Grilled Fish |
| 1070 | 1 | Grilled Vegetable Skewer |
| 1071 | 1 | Grimoire Ink Enhancer |
| 1072 | 1 | Growth Apparatus |
| 1073 | 1 | Guide |
| 1074 | 1 | Guide Updated |
| 1075 | 1 | H |
| 1076 | 1 | HELLO DOWN THERE! Help yourself to a meal, boy. You look as if you need it!\n\nI must away now, I have more food to deliver! Away En, my valiant steed! |
| 1077 | 1 | HUD |
| 1078 | 1 | Hag's Ritual Dagger |
| 1079 | 1 | Halls Shrouded in Darkness |
| 1080 | 1 | Halls of Disrepair |
| 1081 | 1 | Halls of Hatred |
| 1082 | 1 | Hand Axe |
| 1083 | 1 | Hard Wooden Club |
| 1084 | 1 | Hardshell Meganeura |
| 1085 | 1 | Headphones Mode |
| 1086 | 1 | Healing Up |
| 1087 | 1 | Heavy |
| 1088 | 1 | Heavy Res |
| 1089 | 1 | Heavy Rifle |
| 1090 | 1 | Held: |
| 1091 | 1 | Hell Transcribed |
| 1092 | 1 | Hello and thank you for trying out the Queen's Domain Demo!\n\nWe've been working on this project for around 2 years at this point. We're very excited that you can finally try a small part of the game hands on. Let's see what our cats have to say about it!!!  |
| 1093 | 1 | Hello and welcome to the Queen's Domain Guide! \n\nThroughout the game, guides for various systems will unlock as you encounter them.\n\nBasic Controls:\n\nInteract by pressing {interact_binding}.\n\nTo run, press {run_binding} but watch out as running will quickly drain your power meter.\n\nTo jump, press {jump_binding}.\n\nIf you find yourself in need of more information about how things work as you play the game, please remember to check this menu again! |
| 1094 | 1 | Hello everybody I would just like to shout out theb33withknees.. don't really know how to say her name? Shout out to you baby! And uh.. I'm sorry that you died grandma? uhhh.. |
| 1095 | 1 | Help Knowledge Keeper Wourha complete her research. |
| 1096 | 1 | Hempie's SVT 38 |
| 1097 | 1 | Herald of Okbujo |
| 1098 | 1 | Here lie the many Knowledge Keepers of the past, their stewardship and contributions to the tower never forgotten. \n\nMay they rest peacefully in the paradise beyond until great god Wodaso returns their souls to us. |
| 1099 | 1 | Heretical black steel thrusting sword of the Muojwi. A crescent shape mounted on its guard oozes with a thick poisonous residue.\n\nAble to unleash waves of toxic sludge. |
| 1100 | 1 | Hide Options |
| 1101 | 1 | Hide Overlay |
| 1102 | 1 | High Performance Blade |
| 1103 | 1 | High Res Font |
| 1104 | 1 | High Spec Automatons |
| 1105 | 1 | High Tech Data Module |
| 1106 | 1 | Hold Interact To Run |
| 1107 | 1 | Hold for 5 seconds to confirm |
| 1108 | 1 | Hollow Trident Key |
| 1109 | 1 | Hollow spiked balls that bounce every which way when thrown.  |
| 1110 | 1 | Holy Bone of Ruunen |
| 1111 | 1 | Holy Data Module |
| 1112 | 1 | Holy Storm Gem |
| 1113 | 1 | Holy Storm Gem of the True Holy Sword Akunra. Able to unleash a holy storm that simultaneously decimates all in its path and heals the sword's wielder. |
| 1114 | 1 | Holy Storm Ultra Attack |
| 1115 | 1 | Holy Sword Endu |
| 1116 | 2 | Holy Sword Unok Replica |
| 1117 | 1 | Home |
| 1118 | 1 | Homopiscium |
| 1119 | 1 | Homopiscium Priest |
| 1120 | 1 | Horrid beasts... no!\n\nYour Majesty... where are you... ? |
| 1121 | 1 | House Abraon Data Module |
| 1122 | 1 | House Abraon Knight's Crest |
| 1123 | 1 | House Abraon Royal Crest |
| 1124 | 1 | How could such a feeble child have defeated me, a great and mighty king? |
| 1125 | 1 | How did this guy escape the broadcast room? \n\nYou're a long way from home buddy. |
| 1126 | 1 | How long have we been here... can't remember... hnnnnngh |
| 1127 | 1 | How pathetic, to think I'd lost my mind in a place like this... |
| 1128 | 1 | How positively strange, this module's data is quite foreign. It could be the key to my research. There appear to be schematics for upgrading similarly strange weapons as well. Have you found anything like this, I wonder? |
| 1129 | 1 | How to Serve Humans |
| 1130 | 1 | How to Tolerate Jank |
| 1131 | 1 | Huge Rotting Bat |
| 1132 | 1 | I |
| 1133 | 1 | I almost overlooked it but there are schematics contained in this module for upgrading some very mundane weapons. Certainly you're not using anything like this right? Perhaps you've merely grown accustomed to them. |
| 1134 | 1 | I am Erma, Matron of the Deep, wet nurse to all the Orbewa people yes. A true empress if ever there was one, yet the devil has hold of my sister Irdunuub and she has taken all of my eggs. |
| 1135 | 1 | I am certain there are still many meals I've delivered out there for you to find. Most likely the hero has already gotten to them though. In any case I don't need any more meal tickets, I hardly have the time to collect the cloches you've already returned the tickets for! |
| 1136 | 1 | I am hereby filing a formal complaint:\n\nFor the last several days as I tended to the graves in the mausoleum near the top of the tower, I was severely disturbed by an ongoing and excessively loud clanking noise. The noise seems to originate from the highest platform of the tower, just beneath the great bell. As you well know, peace and quiet are of the utmost importance here in our little tower. This noise is simply unacceptable and is causing me undue anxiety and stress in the performance of my duties. Please have this taken care of with the highest priority.\n\n-Knowledge Keeper Bahaji |
| 1137 | 1 | I am in the nine of swords yes. My foolish sister Irdunuub has fallen into Muagyo's vile cult and turned nearly all of the Orbewa people against the alliance with the Abraon. I must escape this cell before they can bring harm to the eggs... |
| 1138 | 1 | I am quite certain that he read from the very worst tome we kept locked away up there. This meat everywhere simply cannot be explained otherwise. See if you can't track him down and give him a good scolding for me if he still lives. |
| 1139 | 1 | I awoke in this near pitch black cell, my head is bleeding. A faint memory of being dragged here by those wretched automatons lingers in the back of my mind. \n\nThe blackened bones of long dead soldiers are patrolling the hall behind the bars of the cell, this can only be Sipeha's magic. I must count my blessings, though my head aches and fiends surround me, all hope is not lost. The cell I've been placed in doesn't seem terribly secure, and I've not been robbed of my spear or bag of supplies. The ceiling is collapsed and I believe I can escape by climbing the rubble.\n\nWodaso guide me. |
| 1140 | 1 | I beg you, find my crew. We may be lowly raiders but they're like family to me. In fact, I think Umpa may be my distant cousin, I'm really not sure... |
| 1141 | 1 | I buy all manner of artifacts and curiosities, If you find anything you've no use for, be sure to let me appraise its value. |
| 1142 | 1 | I can see again! A shame the last sight I'll see is this accursed isle. |
| 1143 | 1 | I can sense a strange and grand destiny for you, child. Venture deep below if you're willing to embrace it. |
| 1144 | 1 | I can't feel my legs... Prince Beruim is that you? I dropped the damned key before the roof collapsed, I... |
| 1145 | 1 | I have failed to protect the shrine... who... |
| 1146 | 1 | I have faith that you will succeed where I had failed. You must, for the sake of all humanity.  |
| 1147 | 1 | I let everyone down, I should've stayed to fight that creature, maybe then... maybe we'd still live... |
| 1148 | 1 | I made it ashore alive and then I went and got myself stuck in this damn stone coffin! Take my amulet to Captain Aganmi but please, don't tell him how I died, it's too pathetic. |
| 1149 | 1 | I must become as the hermit now yes. I must do the hatching and nursing alone. |
| 1150 | 1 | I never did get to meet that hero from my vision, yet I feel you will be able to give them all the help they need! |
| 1151 | 1 | I pray his Majesty is safe.. I'm ashamed we failed him so easily. |
| 1152 | 1 | I see you're unarmed. The weapons we arrived with have all been broken or lost. Take this rusted blade, it's all I've been able to find to defend myself.  |
| 1153 | 1 | I see you've taken up Endu, sister sword of Unok. With her you may yet finish what I've started. Gather Unok's shattered remains for she may yet serve some purpose, although I know not what. |
| 1154 | 1 | I see you. |
| 1155 | 1 | I somehow made my way out of the Record of Sin just as chaos was descending upon it. My fellow Sin Keepers are likely all dead by now. \n\nWhat I did not anticipate is that something is going on out here as well. There is an electricity in the air and I think I caught sight of ancient automatons moving around in the distance. Those dreadful machines lay dormant for thousands of years. What in the world is going on?\n\nI must rest, I shall pray to Wodaso that the horrors lurking both ahead and behind me do not catch sight of me as I slumber. |
| 1156 | 1 | I suppose I owe you a reward don't I? It's only fair. Take this necklace, before now it's brought my crew and I nothing but good luck in our pursuits. Hard to vouch for its effectiveness now though I suppose... |
| 1157 | 1 | I suppose it was about the journey of discovery and not the results after all. At least, I'll have to make do with that. |
| 1158 | 1 | I suppose you're quite the explorer to be talking to my ghost, aren't you? There's something hidden above this room, go on up through the broken window if you can. |
| 1159 | 1 | I suspect that the key to the shrine is in a nearby burial site. If you could find that key and reach the shrine... well, I dare not get my hopes up. |
| 1160 | 1 | I swear I'll be reborn and when I am I'll be bound with your soft and supple skin, pathetic boy! |
| 1161 | 1 | I was cleaning in the storage and I swear I saw a book walking across the room out of the corner of my eye. Later I saw the same book with its brilliant white and blue cover sitting on the table and it seemed normal enough. \n\nThe stress of working in this castle must be getting to me. I should leave, I don't even know why I took this job in the first place. |
| 1162 | 1 | I was using this to expediate my writing, there are just so many formalities required in this type of work. However, the end is in sight now and I feel perhaps it would serve you better than I. |
| 1163 | 1 | I washed up so close to the wreck, yet I had no strength left to reach it. What would've been the point anyway? Take the amulet from my corpse and bring it to the captain, would ya? |
| 1164 | 1 | I will stay here yes. I cannot leave them. |
| 1165 | 1 | I'd hesitated to give it away but you've proven very reliable. It is a family heirloom though, so be careful not to break it. |
| 1166 | 1 | I'll keep cooking and cooking until the day I die, isn't that right dear En?  |
| 1167 | 1 | I'll upload the new upgrade schematics to the terminal. Just give me a moment! |
| 1168 | 1 | I'm Not Paying the Late Fee |
| 1169 | 1 | I'm ready for a nice long rest. Didn't get nearly enough sleep before I was rudely awoken. |
| 1170 | 1 | I'm working on a special meal just for the hero! Bring me more meal tickets and if the hero hasn't arrived yet I'll let you have it instead! |
| 1171 | 1 | I've kept an eye open while wasting away here. Just this morning I noticed some debris washed ashore nearby. Perhaps the remains of the vessel you arrived on?  |
| 1172 | 1 | Idinti Soap |
| 1173 | 1 | If they survived the shipwreck, I know my crew would be out there raiding for treasure! If they've um.. accidentally procured any royal belongings on this isle, you'll forgive them right? |
| 1174 | 1 | If you get the chance, please tell Wourha I tried and that I am sorry. |
| 1175 | 1 | If you want to do business, talk to me from the other side of the counter, friend. That is just how business is done. |
| 1176 | 1 | Imba |
| 1177 | 1 | Immune |
| 1178 | 1 | Impact |
| 1179 | 1 | Impaled Freak |
| 1180 | 1 | Impaler |
| 1181 | 1 | Imprisoned Sin Keeper's Note |
| 1182 | 1 | Increase\n\nLevel |
| 1183 | 1 | Inha, I'm coming!  |
| 1184 | 1 | Initiate Emergency Shut Down |
| 1185 | 1 | Injured Knowledge Keeper's Note |
| 1186 | 1 | Inscribe something in the grimoire. |
| 1187 | 1 | Insert |
| 1188 | 1 | Inspect |
| 1189 | 1 | Instant |
| 1190 | 1 | Insufferable Clanking Noise Complaint |
| 1191 | 1 | Interact |
| 1192 | 1 | Interacting with a Crystal Monument allows you to Rest and Save your game.\n\nResting will fully heal you as well as cure any status ailments. Resting will also respawn most enemies so use it with caution. \n\nMake sure to Save your game at Crystal Monuments regularly because the game will not autosave or create any checkpoints for you!  |
| 1193 | 1 | Introduction and Basic Controls |
| 1194 | 1 | Invert Horizontal Look |
| 1195 | 1 | Invert Horizontal Look - Gamepad |
| 1196 | 1 | Invert Horizontal Look - Mouse |
| 1197 | 1 | Invert Vertical Look |
| 1198 | 1 | Invert Vertical Look - Gamepad |
| 1199 | 1 | Invert Vertical Look - Mouse |
| 1200 | 1 | Ippibi |
| 1201 | 1 | Ippibi was investigating the Shrine of Holy Sword Endu and Budaen entered the Tower of Knowledge to assist Wourha. Perhaps they've found a way to reach His Majesty King Abraon.  |
| 1202 | 1 | Irdunuub, Priestess of Jigeki |
| 1203 | 1 | Iron Fist |
| 1204 | 1 | Iron Pineapple |
| 1205 | 1 | Iron Star Lizard |
| 1206 | 1 | Iryupe Raiders Treasure Key |
| 1207 | 1 | Isle of Neasied |
| 1208 | 1 | It seems that I "went too hard" when writing my research papers, as the youth might say. Still, I was able to complete them thanks to you. |
| 1209 | 1 | It was delicious was it not? I will prepare more so the hero might partake as well! |
| 1210 | 1 | It's been a week, the water hasn't gotten any higher yet. We're out of food and I don't know what to do. Piodor is madly worshipping the statue, calling out a name he heard in a dream "Muagyo". I pray she hears his prayers for I can see no other way out of this predicament. |
| 1211 | 1 | It's locked in place. |
| 1212 | 1 | It's the key to our treasure stashes, they may have washed up somewhere on this isle, keep your eye out for any shipwrecks and you may get lucky. |
| 1213 | 1 | Its bones clatter and clack as it lets out a maddening cackle. |
| 1214 | 1 | J |
| 1215 | 1 | Jade Key Stone |
| 1216 | 1 | Jade Puwema Statuette |
| 1217 | 1 | Japanese |
| 1218 | 1 | Jar Club |
| 1219 | 1 | Jasper Tear Charm |
| 1220 | 1 | Jejano The Forgotten |
| 1221 | 1 | Jewelry given to Erda by the Sea King during the Royal Orbewa Guard ceremony.\n\nIt smells a bit like fish. |
| 1222 | 1 | Jewelry given to Wami by the Sea King during the Royal Orbewa Guard ceremony.\n\nIt's covered in dirt. |
| 1223 | 1 | Jewelry given to Yepo by the Sea King during the Royal Orbewa Guard ceremony.\n\nThere are bits of bone stuck to it.  |
| 1224 | 1 | Jigeki |
| 1225 | 1 | Jigeki Key Bottom |
| 1226 | 1 | Jigeki Key Middle |
| 1227 | 1 | Jigeki Key Top |
| 1228 | 1 | Jigeki Pilgrimage Note I |
| 1229 | 1 | Jigeki Pilgrimage Note II |
| 1230 | 1 | Jigeki's Lens |
| 1231 | 1 | Jigeki's Rot |
| 1232 | 1 | Jigeki's Serpent Thrust |
| 1233 | 1 | Josami Berserker |
| 1234 | 1 | Josami Egg |
| 1235 | 1 | Josami Greatsword |
| 1236 | 1 | Josami Lord Saijha |
| 1237 | 1 | Josami Mechanical Mace |
| 1238 | 1 | Josami Sentinel |
| 1239 | 1 | Journal |
| 1240 | 1 | Jump |
| 1241 | 1 | Just a few adjustments and finalizations and I'll be ready to send this research back to the Royal Academy of Bamaen. Thank you again, now please let me finish my writing in peace. |
| 1242 | 1 | Just so you're aware, I leave a meal ticket with each cloche. Bring any you find back to me so that I might recover the empty cloches. Surely the hero has been leaving these tickets behind as they've yet to return a single one to me. I suppose if you were to return the tickets you could have the reward I've been preparing, even if it was meant for the hero. |
| 1243 | 1 | Justice is done yes. You have brought every stolen egg back to me. |
| 1244 | 1 | K |
| 1245 | 1 | Keep this between the two of us ok? I don't want anyone thinking I'm crazy.\n\nThe gargoyles excavated from beneath the tower REALLY creep me out. I don't know who decided they should be used as decorations, its just weird!!!\n\nIt feels as if they're always watching us, like they're alive. I even thought I saw one of them on the Wing Altar balcony wearing a monocle and reading a book today! I swear I need a vacation. |
| 1246 | 1 | Keepers of Sin |
| 1247 | 1 | Key to chests that the Iryupe Raiders stored their loot and bounty from various "expeditions" in.\n\nCaptain Aganmi entrusted this key to you for looking the other way and helping him learn the fate of his raiders. Hopefully the chests don't contain anything too illegal.  |
| 1248 | 1 | Key to the Forgotten Dungeon's first floor cells. |
| 1249 | 1 | Key to the Forgotten Dungeon's second floor cells. |
| 1250 | 1 | Key to the forbidden archives, located on the top floors of The Tower of Knowledge.\n\nWourha had unwisely entrusted this key to the knight Budaen, who lacked the mental fortitude to contend with the contents of the archives. |
| 1251 | 1 | Keyboard / Mouse Bindings |
| 1252 | 1 | King Gius Abraon |
| 1253 | 1 | King Upobna |
| 1254 | 1 | King Upobna's Journal |
| 1255 | 1 | Kintsugi Zweihander |
| 1256 | 1 | Knight's Encampment |
| 1257 | 1 | Knights Expedition Chest Key |
| 1258 | 1 | Knowledge Keeper Wourha |
| 1259 | 1 | Knowledge Keeper struck by demonic magic, leaving only a mass of melted flesh and pus. It slowly glorps about, spewing a vile poison from its liquified innards. |
| 1260 | 1 | Koko's Sapphic Adventure |
| 1261 | 1 | L |
| 1262 | 2 | L1 |
| 1263 | 1 | L2 |
| 1264 | 1 | L3 |
| 1265 | 1 | LB |
| 1266 | 1 | LQA |
| 1267 | 1 | LT |
| 1268 | 1 | Lakeside |
| 1269 | 1 | Lamp Mace |
| 1270 | 1 | Language |
| 1271 | 1 | Lantern |
| 1272 | 1 | Large Elixer |
| 1273 | 1 | Lastly she ventured far across the seas, visiting other lands and continuing her pilgrimage. Her congregation tirelessly working Jigeki's magic and awaiting her return one day. |
| 1274 | 1 | Later dude!  |
| 1275 | 1 | Learned Star Lizard |
| 1276 | 1 | Leave |
| 1277 | 1 | Left |
| 1278 | 1 | Left Alt |
| 1279 | 1 | Left Button |
| 1280 | 1 | Left Ctrl |
| 1281 | 1 | Left Directional Button |
| 1282 | 1 | Left Mouse Button |
| 1283 | 1 | Left Shift |
| 1284 | 1 | Left Stick |
| 1285 | 3 | Left Stick Down |
| 1286 | 3 | Left Stick Left |
| 1287 | 3 | Left Stick Right |
| 1288 | 3 | Left Stick Up |
| 1289 | 3 | Let's Make a Deal |
| 1290 | 1 | Let's go En, we have food to cook and deliver in the great beyond! |
| 1291 | 1 | Level |
| 1292 | 1 | Level Up |
| 1293 | 1 | Level: |
| 1294 | 1 | Life Charm |
| 1295 | 1 | Life Point Up |
| 1296 | 1 | Life Points |
| 1297 | 1 | Lift Room |
| 1298 | 2 | Lilly Pendant |
| 1299 | 1 | Living Stone |
| 1300 | 1 | Living Stone Chilopoda |
| 1301 | 1 | Living Stone Crawler |
| 1302 | 1 | Living Stone Homopiscium |
| 1303 | 1 | Living Stone Skeleton |
| 1304 | 1 | Living Stone Spider |
| 1305 | 1 | Living tree of the Verdant Hollow, its gnarled and twisted branches tear those who would invade the forest limb from limb. |
| 1306 | 1 | Load Backup |
| 1307 | 1 | Load slot {index}? |
| 1308 | 1 | Localization |
| 1309 | 1 | Lock Mouse To Window |
| 1310 | 1 | Long long ago, a witch settled here on the Isle of Neasied. She had travelled all around the world and made many dear friends, inscribing spells for them in her grimoire as she went.\n\nOne day while sunbathing on the beach, she found all the pages of her grimoire had been erased! "Who could've done this?" she exclaimed. Alas, she was far too lazy to redo all of the inscriptions so she decided to get rid of the book. \n\nShe tucked the grimoire away in the hollow of a nearby tree and sealed the opening so that none could easily find it. Then she exclaimed "whatever man!" and flung herself into …(truncated) |
| 1311 | 1 | Look |
| 1312 | 1 | Look Acceleration |
| 1313 | 1 | Look Down |
| 1314 | 1 | Look Left |
| 1315 | 1 | Look Right |
| 1316 | 1 | Look Sensitivity |
| 1317 | 1 | Look Up |
| 1318 | 1 | Lost Book Bug |
| 1319 | 1 | Lost King Ojjama |
| 1320 | 1 | Lost Sentinel |
| 1321 | 1 | Love |
| 1322 | 1 | Lower Area |
| 1323 | 1 | Luck |
| 1324 | 1 | Luck + Resistance Up |
| 1325 | 1 | Lunar Polearm |
| 1326 | 1 | M |
| 1327 | 1 | Machine Hammer |
| 1328 | 1 | Magic Data Module |
| 1329 | 1 | Magic Eyes |
| 1330 | 1 | Magic Rate |
| 1331 | 1 | Magic Rate Up |
| 1332 | 1 | Magic Rot |
| 1333 | 1 | Magic Rot Res |
| 1334 | 1 | Magic Weapons |
| 1335 | 1 | Magic weapons each have a special secondary attack with its own meter that must be charged to use.\n\nSome magic weapons charge on their own over time, others will only charge when used in combat. |
| 1336 | 1 | Malachite Dragon Wujomiod |
| 1337 | 1 | Manroom Cross |
| 1338 | 1 | Manticore |
| 1339 | 1 | Manticore Occultist |
| 1340 | 1 | Many days past, His Majesty ventured into Deimpo Mausoleum with a company of knights. The door sealed tight behind them and there hasn't been a single sign of their return. Not had even a moment to search for a way inside. We've struggled just to defend this small encampment from the beasts that roam the isle. |
| 1341 | 1 | Manyyy found... manyyy remainnnn... |
| 1342 | 1 | Mask of Second Sight |
| 1343 | 1 | Massive mausoleum with corridors, caverns, and chambers winding deep below the surface. |
| 1344 | 1 | Master Volume |
| 1345 | 1 | May Puwema watch over you, young prince... |
| 1346 | 1 | May the magics of goddess Puwema keep this tomb shut firmly.\n\nThe six seals placed upon it sealing the unholy Queen Siraatsu in her grave eternal. |
| 1347 | 1 | Meal Charm |
| 1348 | 1 | Meal Ticket |
| 1349 | 1 | Mecha Knight |
| 1350 | 1 | Mechanical Arm |
| 1351 | 1 | Mechanical Duck Figurine |
| 1352 | 1 | Mechanical Engineer Ossi |
| 1353 | 1 | Mechanical Spider |
| 1354 | 1 | Mechanical mace of the Josami knights, loyal protectors of Queen Siraatsu. It is fitted with a mechanism allowing the bladed head to spin at high speeds. |
| 1355 | 1 | Medicative herbs grown in the Kingdom of Bamaen, carefully picked, dried, and bundled. \n\nThis bundle has been kept for quite long and lost most of its color and effectiveness.  |
| 1356 | 1 | Medicative herbs grown in the Kingdom of Bamaen, carefully picked, dried, and bundled. \n\nThis bundle has been picked recently and still has a wonderful fresh scent and potent healing effects.  |
| 1357 | 1 | Meganeura |
| 1358 | 1 | Melee Weapons |
| 1359 | 1 | Melee weapons use the power meter - the yellow meter near the top of the screen - to determine their damage. The damage of an attack is higher the more full the meter is when attacking.\n\nTo attack with a melee weapon press {primaryattack_binding}.\n\nAdditionally, most attacks can be charged by holding the attack input. Charge is indicated by the power meter filling with blue, more charge on the meter will also increase the damage of an attack. \n\nAll melee weapons have a secondary attack which can perform a variety of functions, so give them a try by pressing {secondaryattack_binding}! |
| 1360 | 1 | Melting Moon |
| 1361 | 1 | Memento Mirror |
| 1362 | 1 | Memorial Sculpture |
| 1363 | 1 | Memorial Sculpture Engraving |
| 1364 | 1 | Men, we march together one final time. For the honor of House Abraon! |
| 1365 | 1 | Menu Button |
| 1366 | 1 | Metallic Pus Bag |
| 1367 | 1 | Mid Tower |
| 1368 | 1 | Middle Mouse Button |
| 1369 | 1 | Mimic |
| 1370 | 1 | Missing Books |
| 1371 | 1 | Model of an Ancient City Placard |
| 1372 | 1 | Moldy Loot Sack |
| 1373 | 1 | Monarch of the Void |
| 1374 | 1 | Morning Star |
| 1375 | 1 | Mossy Golem |
| 1376 | 1 | Most Spiritual Sword  |
| 1377 | 1 | Most Unfortunate |
| 1378 | 1 | Mouse |
| 1379 | 1 | Mouse Look |
| 1380 | 1 | Mouse Look Sensitivity |
| 1381 | 1 | Move Backward |
| 1382 | 1 | Move Forward |
| 1383 | 1 | Move Left |
| 1384 | 1 | Move Right |
| 1385 | 1 | Mr Quackers |
| 1386 | 1 | Muagyo's Congregation |
| 1387 | 1 | Muagyo's Journey I |
| 1388 | 1 | Muagyo's Journey II |
| 1389 | 1 | Muagyo's Journey III |
| 1390 | 1 | Muagyo's Journey IV |
| 1391 | 1 | Mundane Data Module |
| 1392 | 1 | Muojwi's Uprising |
| 1393 | 1 | Music Volume |
| 1394 | 1 | Mute When Unfocused |
| 1395 | 1 | My crew, I let them down. At least I was able to set up a small memorial for them. I hope they've made their peace. |
| 1396 | 1 | My dearest son, I only wish I'd been strong enough, now it falls upon you... may Puwema and Wodaso keep you safe.  |
| 1397 | 1 | My goodness such a refined golden duck! As thanks I'll make some new wares available to purchase.  |
| 1398 | 1 | My son, I know you can do it. |
| 1399 | 1 | Mysterious Grave Tablet |
| 1400 | 1 | Mysterious Udyu |
| 1401 | 1 | Mystic Book Bug |
| 1402 | 1 | Mystic Watcher |
| 1403 | 1 | Mystic's Mirror |
| 1404 | 1 | Mystical Nut of the White Tree |
| 1405 | 1 | N |
| 1406 | 1 | Nautigallus |
| 1407 | 1 | Navigate |
| 1408 | 1 | Necklace of a fallen Iryupe Raider, to be returned to Captain Aganmi.\n\nThe raider it belonged to seems to have met quite an unfortunate end, what a shame. |
| 1409 | 1 | Necrotic Bael |
| 1410 | 1 | Necrotic Baphomet |
| 1411 | 1 | Necrotic Eye of Balor |
| 1412 | 1 | Necrotic Shambler |
| 1413 | 1 | Next Category |
| 1414 | 1 | Next Item |
| 1415 | 1 | Next Throwing Weapon |
| 1416 | 1 | Next Weapon |
| 1417 | 1 | Nintendo |
| 1418 | 1 | No Items |
| 1419 | 1 | Noble Sentinel |
| 1420 | 1 | None |
| 1421 | 1 | Normal |
| 1422 | 2 | Normal 72 Year Old Blade |
| 1423 | 1 | Normal fruit punch for a normal 72 year old girl.\n\nMakes you stronger and improves your health, just like normal fruit punch would do <img id="B33IsNormal"></> |
| 1424 | 1 | Now how did you manage that!? I'd lost the key to the front door years ago! Not to mention you've managed to arrive on this remote islet without the usual means of transportation! \n\nWhile that is quite interesting, you're still here on business, right? What can I do for you today? |
| 1425 | 1 | Now take care, I don't think we'll meet again and this is all I can do for you. |
| 1426 | 1 | Now that the Stone Duck Figurine has resonated with the Crystal Monument, you can travel to Umbada's Trading Outpost from the Crystal Monument menu whenever you'd like.\n\nFrom Umbada's Trading Outpost you can buy and sell a variety of items.\n\nIf you find any more duck figures, make sure to bring them here! |
| 1427 | 1 | Noxious Glaucusion |
| 1428 | 1 | Nu |
| 1429 | 1 | Num * |
| 1430 | 1 | Num + |
| 1431 | 1 | Num - |
| 1432 | 1 | Num . |
| 1433 | 1 | Num / |
| 1434 | 1 | Num 0 |
| 1435 | 1 | Num 1 |
| 1436 | 1 | Num 2 |
| 1437 | 1 | Num 3 |
| 1438 | 1 | Num 4 |
| 1439 | 1 | Num 5 |
| 1440 | 1 | Num 6 |
| 1441 | 1 | Num 7 |
| 1442 | 1 | Num 8 |
| 1443 | 1 | Num 9 |
| 1444 | 1 | Num Lock |
| 1445 | 1 | Number Killed |
| 1446 | 1 | Nyx Thorn Blade |
| 1447 | 1 | O |
| 1448 | 1 | OK |
| 1449 | 1 | Obmuer Temple |
| 1450 | 1 | Obmuer Temple Explorer's Note I |
| 1451 | 1 | Obmuer Temple Explorer's Note II |
| 1452 | 1 | Obmuer Temple Explorer's Note III |
| 1453 | 1 | Obmuer Temple Explorer's Note IV |
| 1454 | 1 | Obsidian Golem |
| 1455 | 1 | Obtain the Nyx Thorn Blade on this save file? |
| 1456 | 1 | Occult Knife |
| 1457 | 1 | Occult Knives |
| 1458 | 1 | Occult Spear |
| 1459 | 1 | Occult Warhammer |
| 1460 | 1 | Occultist Josami Egg |
| 1461 | 1 | Occultist Josami Templar |
| 1462 | 1 | Occultist Josami Warden |
| 1463 | 1 | Ocean Walk |
| 1464 | 1 | Ochre Coelacuda |
| 1465 | 1 | Off |
| 1466 | 1 | Oh Irdunuub, I pray to the great lord Jigeki that you've found a suitable heir to my throne.  |
| 1467 | 1 | Oh Onbi, Etne, Idpo, and Umpa, please let Wodaso watch over you... |
| 1468 | 1 | Oh goodness me, how horrible! There are schematics stored in this data module for upgrading the most vile weapons forged in the name of Queen Siraatsu. Are you sure it's wise to wield such weapons? |
| 1469 | 1 | Oh my this is quite old and quite rare. I've never seen this type of stone before! As thanks I'll make some new wares available to purchase.  |
| 1470 | 1 | Oh the Places You'll Go |
| 1471 | 1 | Oh, have you found one of my crew? |
| 1472 | 1 | Oh, this contains a small set of schematics for upgrading royal weapons of the House of Abraon? Never heard of it, probably another group of useless figureheads. Just like all the other so called royal families, ha! |
| 1473 | 1 | Oh? Have you found some meal tickets? |
| 1474 | 1 | Oh? There are schematics here for upgrading ancient weapons. The methods used to forge such weapons were thought to be a long lost art, you know. |
| 1475 | 1 | Ok, uh... yes, very good. We hope you enjoy the demo! |
| 1476 | 1 | Okbujo's Gaze |
| 1477 | 1 | Okbujo's Judgement |
| 1478 | 1 | Okbujo's Orb |
| 1479 | 1 | Okbujo's Silver Garden |
| 1480 | 1 | Old Boy |
| 1481 | 1 | On |
| 1482 | 1 | On to the next adventure, surely the afterlife is rife with glittering treasures right? |
| 1483 | 1 | Once peaceful cavern now overflowing with fetid rotten poison. |
| 1484 | 1 | Once the king of the Orbewa many millennia ago, Ojjama gave up his title to stand guard over the Petrified Garden. \n\nHe worked with the Epha to construct the garden after the war with Muojwi, it stands to contain a long forgotten remnant from the war that would threaten to rip the very fabric of reality should it be uncovered. |
| 1485 | 1 | Once you've had a chance to read it over, be sure to return it to the proper shelf. Why are you giving me such a look? I am not simply giving you this to return it for me, it should be of great interest to you! Everything must be kept catalogued and organized here, you cannot simply keep a one of a kind book... |
| 1486 | 1 | One egg, I... thank you. It is better than none yes. Yet if this is all you bring me, I fear the tower may fall. |
| 1487 | 1 | One of four triangular slates needed to open the door to Obmuer Temple. |
| 1488 | 1 | One of four triangular slates needed to open the door to a temple tucked away in the Rotting Cavern. |
| 1489 | 1 | One of the missing gems of Holy Sword Endu. Once affixed in the middle of the sword's guard, it slowly builds up the power to call a violent storm.\n\nWhen Holy Sword Akunra shattered, the Holy Storm Gem within split in two, leaving this half with only the power of the storm. The other half containing holy power was forged into Endu's sister sword Unok, wielded by your father King Gius Abraon. |
| 1490 | 1 | One of the missing gems of Holy Sword Endu. Once socketed into place, allows Endu's wielder to fly even farther and dash more frequently. \n\nThis gem and the metal Endu was forged from are said to be extracted from the remains of Holy Sword Akunra, which was destroyed in the battle between the avatar of Puwema and Queen Siraatsu, the avatar of Sipeha.  |
| 1491 | 1 | One of the oldest known books, it describes the life of Ruunen, avatar of the goddess Puwema. \n\nRuunen lived in a distant era before most recorded history. Much of her life is described as entirely foreign and impossible for modern readers to relate to.\n\nThere is a chapter dedicated to the forging of the holy swords of house Abraon but it is not clear when this happened. |
| 1492 | 1 | Onyx Sipeha Statuette |
| 1493 | 1 | Opal Star Charm |
| 1494 | 1 | Open Pause Menu |
| 1495 | 1 | Open Screenshot Folder |
| 1496 | 1 | Open Status Menu |
| 1497 | 1 | Open every chest. |
| 1498 | 1 | Open every secret door. |
| 1499 | 1 | Options |
| 1500 | 2 | Options Button |
| 1501 | 1 | Orange |
| 1502 | 1 | Orange Ocre Seal Breaker |
| 1503 | 1 | Orbewa Egg |
| 1504 | 1 | Orbewa Holy Amulet |
| 1505 | 1 | Orda Bud |
| 1506 | 1 | Orphic Ungulacrita |
| 1507 | 1 | Ossi's Lab |
| 1508 | 1 | Ossi's Memento |
| 1509 | 1 | Otherworldly Data Module |
| 1510 | 1 | Ourple |
| 1511 | 1 | Overcome every challenge on the Isle of Neasied. |
| 1512 | 1 | Overflowing Energy |
| 1513 | 1 | Overlook |
| 1514 | 1 | P |
| 1515 | 1 | Page Down |
| 1516 | 1 | Page Up |
| 1517 | 1 | Pail Chopper |
| 1518 | 1 | Pale Nibbler |
| 1519 | 1 | Pale variant species of the forest eye adapted to live in the darkest, dankest places.\n\nFires a sticky beam at any human that falls under its gaze. |
| 1520 | 1 | Pallete |
| 1521 | 1 | Panacea |
| 1522 | 1 | Pandora Blade |
| 1523 | 1 | Paralytic Needle |
| 1524 | 1 | Paralyze |
| 1525 | 1 | Paranoid Knowledge Keeper's Note |
| 1526 | 1 | Pause |
| 1527 | 1 | Pause Menu |
| 1528 | 1 | Pause When Unfocused |
| 1529 | 1 | Pause must have a valid binding. |
| 1530 | 1 | Pear |
| 1531 | 1 | Pebble Hammer |
| 1532 | 1 | Peha |
| 1533 | 1 | Peha's Memento |
| 1534 | 1 | Pendant of the Josami Blood Angels. It's enchanted with blood magic, stealing life and giving it to its bearer. |
| 1535 | 1 | People from the Sea |
| 1536 | 1 | Perfected Weaponry |
| 1537 | 1 | Perhapsssss... still freshhhh... |
| 1538 | 1 | Perverted Mass of Warped Flesh |
| 1539 | 1 | Petrified Garden |
| 1540 | 1 | Photo Mode |
| 1541 | 1 | Physical |
| 1542 | 1 | Pierce |
| 1543 | 1 | Piercing Beam Cannon |
| 1544 | 1 | Piercing Needle |
| 1545 | 1 | Piercing Needles |
| 1546 | 1 | Pigeon Fossil |
| 1547 | 1 | Pilgrim's Note I |
| 1548 | 1 | Pilgrim's Note II |
| 1549 | 1 | Pilgrim's Note III |
| 1550 | 1 | Pinnacle Anteroom |
| 1551 | 1 | Piss |
| 1552 | 1 | Pit of Despair |
| 1553 | 1 | Plasma Knife |
| 1554 | 1 | Plasma Spear |
| 1555 | 1 | Plasma Sword |
| 1556 | 1 | Play |
| 1557 | 1 | Play Time |
| 1558 | 1 | Playstation |
| 1559 | 1 | Please, I am intensely busy with my research. I can feel it, I'm on the verge of a major breakthrough. Come back when you've obtained a data module and not a moment sooner. |
| 1560 | 1 | Plump bright red berries that grow high up in the mountains of the Kingdom of Bamaen's mainland. \n\nWhen chewed they release a chemical compound which provides a short term boost in strength. \n\nNot particularly palatable to most although some warriors grow accustomed to the taste.  |
| 1561 | 1 | Plush Ratty Bug |
| 1562 | 1 | Pod-like plants that open to reveal a gigantic eye. The flesh of these plants has been corrupted by something other-worldly.\n\nFires a red hot laser at any human that falls under its gaze.  |
| 1563 | 1 | Poenir & Duenir, Malformed Demonic Aberrations |
| 1564 | 1 | Poenir is a huge demonic bat conjured by Queen Siraatsu herself, left to guard the burial chamber after Sipeha was foolishly awakened by King Gius Abraon. \n\nShe has waited in despair, slowly rotting for millennia in the underworld. Now that the queen has released her, she will stop at nothing to prevent any would-be followers of the queen from entering the depths below. If any should make it past her and Duenir, a return to the underworld surely awaits them.  |
| 1565 | 1 | Poenir, Rotten Wings of Despair |
| 1566 | 1 | Poison |
| 1567 | 1 | Poison Chalice |
| 1568 | 1 | Poison Res |
| 1569 | 1 | Poisoned Dirk |
| 1570 | 1 | Poisoned Dirks |
| 1571 | 1 | Poltergeist |
| 1572 | 1 | Porting by Parallel Circles |
| 1573 | 1 | Portuguese Brazil |
| 1574 | 1 | Power Station Maintenance |
| 1575 | 1 | Power Terminal |
| 1576 | 1 | Power: |
| 1577 | 1 | Preservation Asanawa |
| 1578 | 1 | Press Gamepad Input to bind to {actionname} |
| 1579 | 1 | Press Input to bind to {actionname} |
| 1580 | 3 | Press Keyboard or Mouse Input to bind to {actionname} |
| 1581 | 1 | Press confirm if the selected Resolution and Window Mode are displayed correctly.\n\nResetting in 15 seconds. |
| 1582 | 1 | Press the Left Stick |
| 1583 | 2 | Press the Right Stick |
| 1584 | 2 | Previous Category |
| 1585 | 1 | Previous Item |
| 1586 | 1 | Previous Throwing Weapon |
| 1587 | 1 | Previous Weapon |
| 1588 | 1 | Primary Attack |
| 1589 | 1 | Primeval Shrine Engraving |
| 1590 | 1 | Prince Beruim I beseech you, set sail for Neasied at once. |
| 1591 | 1 | Prince Beruim! Gracious goddess Puwema we're saved! I fear Sir Daon and Sir Ujji are far worse for wear than I. I've not been able to hear any response from either for quite some time, yet I can feel they are still nearby.  |
| 1592 | 1 | Prince Beruim's hunger for god-flesh is not yet sated. The world may descend into chaos... |
| 1593 | 1 | Prince Emdaer fell right before my eyes and our entire army has surrendered. Those repugnant soldiers of Bamaen have hauled me to this cell. I do not care to await whatever punishment they have in store for me and my fellow soldiers of Ommu, I shall take matters into my own hands... |
| 1594 | 1 | Prison Corridor |
| 1595 | 1 | Project Manager |
| 1596 | 1 | Protection Charm |
| 1597 | 1 | Published by Future Friends Games |
| 1598 | 1 | Pungent Root |
| 1599 | 1 | Pungent Shambler |
| 1600 | 1 | Purchase Wares |
| 1601 | 1 | Purchase {ShopEntryName} for {price} coins? |
| 1602 | 1 | Pus Bag |
| 1603 | 1 | Putrid Chicket |
| 1604 | 1 | Puwema |
| 1605 | 1 | Puwema's Blessed Orb |
| 1606 | 1 | Puwema's Blessing |
| 1607 | 1 | Puwema's Lens |
| 1608 | 1 | Puwema's Rest Room |
| 1609 | 1 | Puwema's Temple |
| 1610 | 1 | Puwema's Wind Dancer |
| 1611 | 1 | Q |
| 1612 | 1 | QA |
| 1613 | 1 | QA Specialist |
| 1614 | 1 | QA by Lollipop Robot |
| 1615 | 1 | Quaaaack... quaaaaack... |
| 1616 | 1 | Quartz Wodaso Statuette |
| 1617 | 1 | Queen Siraatsu |
| 1618 | 1 | Queen Siraatsu's ancient castle of steel, it once again brims with unholy activity. |
| 1619 | 1 | Queen Siraatsu's most powerful and loyal knight, Saijha is captain of the Order of Josami knights, sworn to protect the queen and her castle.\n\nArmed with massive sword and powerful plasma rifle, she will let none cross the bridge. |
| 1620 | 1 | Queen's Domain |
| 1621 | 1 | Quick Charger |
| 1622 | 1 | Quit |
| 1623 | 1 | Quit To Desktop |
| 1624 | 1 | Quit To Desktop? |
| 1625 | 1 | Quit To Title |
| 1626 | 1 | Quit To Title Screen? |
| 1627 | 1 | Quite good findings, your Highness. You've been ever so diligent. This dagger may be of aid in furthering our research. |
| 1628 | 1 | Quite interesting indeed. Keep searching for more creatures, I've not made quite enough progress on my research to give you a reward just yet.  |
| 1629 | 1 | R |
| 1630 | 2 | R1 |
| 1631 | 1 | R2 |
| 1632 | 1 | R3 |
| 1633 | 1 | RB |
| 1634 | 1 | RT |
| 1635 | 1 | Rabios the Witness |
| 1636 | 1 | Radiant Fruit |
| 1637 | 1 | Radioactive Blade |
| 1638 | 1 | Raging Star Lizard |
| 1639 | 1 | Raging Tempest |
| 1640 | 1 | Raider Captain Aganmi |
| 1641 | 1 | Rancid Loot Sack |
| 1642 | 1 | Rare medicative herbs grown in the Kingdom of Bamaen, carefully picked, dried, and bundled. \n\nThese herbs, usually reserved for royalty or those with great wealth, are a highly effective remedy with an enchanting floral scent. |
| 1643 | 1 | Ratboy's Zhuge Crossbow |
| 1644 | 1 | Ratpod |
| 1645 | 1 | Ratty Bug |
| 1646 | 1 | Ratty Bug Compendium |
| 1647 | 1 | Raw Glaucusion |
| 1648 | 1 | Razor sharp intertwined blades crafted for use in banishment rituals. \n\nThe Goddess Sipeha's dark magics stain the blades black and red, yearning to send souls to cold oblivion. |
| 1649 | 1 | Reach Siraatsu Castle. |
| 1650 | 1 | Reach the Ancient World. |
| 1651 | 1 | Reach the Deimpo Mausoleum. |
| 1652 | 1 | Reach the Forgotten Dungeon. |
| 1653 | 1 | Reach the Obmuer Temple. |
| 1654 | 1 | Reach the Petrified Garden. |
| 1655 | 1 | Reach the Record of Sin. |
| 1656 | 1 | Reach the Rotting Cavern. |
| 1657 | 1 | Reach the Tower of Knowledge. |
| 1658 | 1 | Reach the Verdant Hollow. |
| 1659 | 1 | Reanimated bones of a mad marksman, glassed by azure flame in the ancient war with the Muojwi.\n\nArmed with an ancient gun that fires waves of slicing magic, it cuts down its victims with lethal accuracy. |
| 1660 | 1 | Reanimated bones of a murderous bastard, glassed by azure flame in the ancient war with the Muojwi.\n\nIt sadistically perforates its foes with its shield of jagged spikes and cleaves through their bones with its well-honed cleaver. |
| 1661 | 1 | Reanimated bones of a sellsword, glassed by azure flame in the ancient war with the Muojwi. \n\nIt stalks the underworld beneath the Isle of Neasied, cleaving any foolish enough to cross its path to bits with its deadly scythes.  |
| 1662 | 1 | Receive a strange reward. |
| 1663 | 1 | Record of Sin |
| 1664 | 1 | Recover all five Wind Gems. |
| 1665 | 1 | Recover the Storm Gem. |
| 1666 | 1 | Red |
| 1667 | 1 | Red Nibbler |
| 1668 | 1 | Refill Energy Cells |
| 1669 | 1 | Reforged from the Soul Bound Holy Sword Endu and the remains of Holy Sword Unok, the True Holy Sword Akunra has been reborn. With rippling ethereal wings and ten Wind Gems floating in its core, the sword allows its wielder to fly great distances, as if dancing across the sky on the very wind itself.\n\nThe original form of this sword was destroyed during the battle between Peha, avatar of Puwema and Siraatsu, avatar of Sipeha many centuries ago.\n\nNear every swing of the blade produces an arc of holy energy. |
| 1670 | 1 | Regal Star Lizard |
| 1671 | 1 | Regarding the maintenance of the power station electric networks.\n\nThe power stations around this sector have all been routed to sealing off the castle bridge. In the event that power should fail at one station, the other two should maintain sufficient power to keep it sealed for at least a few years. Even so, it would be safest to keep them all running.\n\nProcedure: Aim and fire the Shock Rifle to restore power to each of the nodes on the network. The surrounding nodes will also become powered. Make sure not to overload a node by powering it twice or it will shut off as a safety measure.  |
| 1672 | 1 | Reinforced Jar Club |
| 1673 | 1 | Rejuvenating Water |
| 1674 | 1 | Remove From Quick Slots |
| 1675 | 1 | Reset Audio Options To Default |
| 1676 | 1 | Reset Audio Options To Default? |
| 1677 | 1 | Reset Bindings To Default |
| 1678 | 2 | Reset Bindings To Default? |
| 1679 | 1 | Reset Color Adjustments |
| 1680 | 1 | Reset Control Options To Default |
| 1681 | 1 | Reset Control Options To Default? |
| 1682 | 1 | Reset Gameplay Options To Default |
| 1683 | 1 | Reset Gameplay Options To Default? |
| 1684 | 1 | Reset Generator Node Network |
| 1685 | 1 | Reset Now |
| 1686 | 1 | Reset Position |
| 1687 | 1 | Reset Video Options To Default |
| 1688 | 1 | Reset Video Options To Default? |
| 1689 | 1 | Resist |
| 1690 | 1 | Resolution |
| 1691 | 1 | Resolution Scale |
| 1692 | 1 | Rest |
| 1693 | 1 | Restored relic from a long-forgotten era. \n\nSturdy and reliable even now. |
| 1694 | 1 | Retro Resolution |
| 1695 | 1 | Return all of the lost books. |
| 1696 | 1 | Return to last Crystal Monument? |
| 1697 | 1 | Return: |
| 1698 | 1 | Rifle-wielding Josami sniper, nothing evades her watchful eye.\n\nThe Order of the Josami are sworn to protect Queen Siraatsu and her castle. |
| 1699 | 1 | Right |
| 1700 | 1 | Right Alt |
| 1701 | 1 | Right Button |
| 1702 | 1 | Right Ctrl |
| 1703 | 1 | Right Directional Button |
| 1704 | 1 | Right Mouse Button |
| 1705 | 1 | Right Shift |
| 1706 | 1 | Right Stick |
| 1707 | 3 | Right Stick Down |
| 1708 | 3 | Right Stick Left |
| 1709 | 3 | Right Stick Right |
| 1710 | 3 | Right Stick Up |
| 1711 | 3 | Ring of Fire |
| 1712 | 1 | Ring of Wind |
| 1713 | 1 | Ripping Talon |
| 1714 | 1 | Ritual Blood Chromobula |
| 1715 | 1 | Ritual Blood Stalker |
| 1716 | 1 | Ritual Blood Tauropod |
| 1717 | 1 | Ritual Dagger? I hardly know'er.  |
| 1718 | 1 | Ritudeon Compass |
| 1719 | 1 | Roasted Cave Scorpion |
| 1720 | 1 | Roasted Ojum Lizard |
| 1721 | 1 | Roasted Poultry |
| 1722 | 1 | Rotate |
| 1723 | 1 | Rotate Left |
| 1724 | 1 | Rotate Right |
| 1725 | 1 | Rotten Chalice |
| 1726 | 1 | Rotten Homopiscium |
| 1727 | 1 | Rotten Thorny Gape |
| 1728 | 1 | Rotting Cavern |
| 1729 | 1 | Rotting Eye |
| 1730 | 1 | Royal Decree of King Upobna |
| 1731 | 1 | Royal Dried Herbs |
| 1732 | 1 | Royal Throwing Dagger |
| 1733 | 1 | Rumble Intensity |
| 1734 | 1 | Run |
| 1735 | 1 | Russian |
| 1736 | 1 | Rusted Cell Key |
| 1737 | 1 | Rusted Great Morning Star |
| 1738 | 1 | Rusted Greataxe |
| 1739 | 1 | Rusted Greatsword |
| 1740 | 1 | Rusted Leaf Blade |
| 1741 | 1 | Rusted Longhammer |
| 1742 | 1 | Rusted Short Sword |
| 1743 | 1 | Rusted Spear |
| 1744 | 1 | S |
| 1745 | 1 | Sacred Cistern |
| 1746 | 1 | Sacred Groves |
| 1747 | 1 | Sacred Necklace |
| 1748 | 1 | Sacred Waters |
| 1749 | 1 | Sacred temple of the aquatic Orbewa people who rose from the sea many centuries ago. |
| 1750 | 1 | Sapphire Rhino |
| 1751 | 1 | Saturation |
| 1752 | 1 | Saurian Wing |
| 1753 | 1 | Save |
| 1754 | 1 | Saved |
| 1755 | 1 | Saving |
| 1756 | 1 | Saw Ending 1. |
| 1757 | 1 | Saw Ending 2. |
| 1758 | 1 | Saw Ending 3. |
| 1759 | 1 | Saw Ending 4. |
| 1760 | 1 | Scaled Greatsword |
| 1761 | 1 | Scarlet Mimic |
| 1762 | 1 | Scarlet Watcher |
| 1763 | 1 | Scrap Yard |
| 1764 | 1 | Scroll Description |
| 1765 | 1 | Scroll Down |
| 1766 | 1 | Scroll Lock |
| 1767 | 1 | Scroll Text |
| 1768 | 1 | Scroll Up |
| 1769 | 1 | Scythetini |
| 1770 | 1 | Seal Breaker of Cups |
| 1771 | 1 | Seal Breaker of Pentacles |
| 1772 | 1 | Seal Breaker of Swords |
| 1773 | 1 | Seal Breaker of Wands |
| 1774 | 1 | Secondary Attack |
| 1775 | 1 | Seek the souls you once knew.\n\nTake what they offer to the master's shrine. |
| 1776 | 1 | Selected Resolution and Window Mode failed to apply. |
| 1777 | 1 | Sell Treasures |
| 1778 | 1 | Sell {ShopEntryName} for {price} coins? |
| 1779 | 1 | Sepiicanis |
| 1780 | 1 | Serious |
| 1781 | 1 | She made her way below, no doubt returning to her dreadful castle to plot the destruction of all the lands above. Beyond that fissure is a truth of this world I prayed you might never need know of, yet I have failed, and so you must... to protect our kingdom and all other people of this world. |
| 1782 | 1 | Shock Rifle |
| 1783 | 1 | Shop |
| 1784 | 1 | Show Bestiary Notifications |
| 1785 | 1 | Show Challenge Notifications |
| 1786 | 1 | Show Compass |
| 1787 | 1 | Show Guide Notifications |
| 1788 | 1 | Show Interact |
| 1789 | 1 | Show Options |
| 1790 | 1 | Shrine Guardian |
| 1791 | 1 | Shrouded Crawler |
| 1792 | 1 | Silver Bangle |
| 1793 | 1 | Silver Bell of Frost |
| 1794 | 1 | Silver Conch Necklace |
| 1795 | 1 | Silver Crown |
| 1796 | 1 | Silver Duck Figurine |
| 1797 | 1 | Silver Etched Skullssss.... Memoriessss longggg forgottennnn... |
| 1798 | 1 | Silver Fish Treasure |
| 1799 | 1 | Silver Jigeki Statuette |
| 1800 | 1 | Silver Longhammer |
| 1801 | 1 | Silver Nautilus Crown |
| 1802 | 1 | Silver Needle |
| 1803 | 1 | Silver Pocket Watch |
| 1804 | 1 | Silver Puwema Statuette |
| 1805 | 1 | Silver Ratty Bug Figurine |
| 1806 | 1 | Silver Scallop Tiara |
| 1807 | 1 | Silver Sipeha Statuette |
| 1808 | 1 | Silver Star Lizard Statuette |
| 1809 | 1 | Silver Wodaso Statuette |
| 1810 | 1 | Silver dragon of the Dekanima, it precedes Okbujo in preparation for their coming. \n\nPerhaps the artifacts stored in the Record of Sin were better left untouched. |
| 1811 | 1 | Sin Keeper |
| 1812 | 1 | Sin Keeper's Journal |
| 1813 | 1 | Sin Keeper's Spear |
| 1814 | 1 | Sipeha |
| 1815 | 1 | Sipeha's Lens |
| 1816 | 1 | Sipeha's Twisted Heart |
| 1817 | 1 | Sipeha's Void Maul |
| 1818 | 1 | Siraatsu Castle |
| 1819 | 1 | Siraatsu's Data Module |
| 1820 | 1 | Siraatsu, Ancient Queen |
| 1821 | 1 | Skeletal Arbelist |
| 1822 | 1 | Skeletal Archer |
| 1823 | 1 | Skeletal Executioner |
| 1824 | 1 | Skeletal Fighter |
| 1825 | 1 | Skeletal Soldier |
| 1826 | 1 | Skeletal Warrior |
| 1827 | 1 | Skewered ojum lizard roasted over an open flame. It has been carefully gutted, leaving the liver intact.\n\nThese elusive creatures, prized for their unique poison-curing liver, are usually caught during rainy seasons when they are driven out of hiding in droves. |
| 1828 | 1 | Skip Cutscene |
| 1829 | 1 | Skip Cutscene? |
| 1830 | 1 | Skull Charm |
| 1831 | 1 | Skull Rod |
| 1832 | 1 | Slash |
| 1833 | 1 | Slave of Puwema, perhaps you'll see the truth one day. |
| 1834 | 1 | Slimy Loot Sack |
| 1835 | 1 | Slow |
| 1836 | 1 | Small Energy Cell |
| 1837 | 1 | Small Isle to the north of Bamaen, covered in lush foliage and mossy green ruins. |
| 1838 | 1 | Small pale fish with jagged teeth coated in a sticky slime.\n\nIt swims in the lake beside goddess Puwema's temple in the Verdant Hollow. |
| 1839 | 1 | So the debris was part of your ship after all? Given the damage it's a miracle you made it ashore in one piece! Lucky to have found your dagger belt. It's a truly precious gift not easily replaced. |
| 1840 | 1 | So veryyyy close... we cannn taste ittttt... |
| 1841 | 1 | Soap that can clean nearly any substance from the body, no matter how sticky. \n\nDriven by necessity, the Idinti Wanderers of the eastern continent's desert forests formulated this unique soap from coarse sand and bone-tree wax local to the region. \n\nIts effectiveness is world renowned and it is a highly sought after commodity. |
| 1842 | 1 | Sold |
| 1843 | 1 | Sold Out |
| 1844 | 1 | Soldier's Shock Blade |
| 1845 | 1 | Solid Stone Block |
| 1846 | 1 | Sonic Crystal Fossil |
| 1847 | 1 | Soul Bound Endu |
| 1848 | 1 | Soul of Sipeha |
| 1849 | 1 | Space Bar |
| 1850 | 1 | Spanish |
| 1851 | 1 | Sparkle Bug |
| 1852 | 1 | Sparkling Clean |
| 1853 | 1 | Special Attack |
| 1854 | 1 | Special Computer |
| 1855 | 1 | Spectrum |
| 1856 | 1 | Speed: |
| 1857 | 1 | Spend one thousand Ancient Coins at Umbada's Trading Outpost. |
| 1858 | 1 | Spiked Ball |
| 1859 | 1 | Spiked Buckler |
| 1860 | 1 | Spirit Oil |
| 1861 | 1 | Spirit of Arjaot |
| 1862 | 1 | Spirit of Budaen |
| 1863 | 1 | Spirit of Chef Gagi |
| 1864 | 1 | Spirit of Commander Ekonta |
| 1865 | 1 | Spirit of Daon |
| 1866 | 1 | Spirit of En, The Giant Crow |
| 1867 | 1 | Spirit of Erma, Matron of the Deep |
| 1868 | 1 | Spirit of Imba |
| 1869 | 1 | Spirit of Ippibi |
| 1870 | 1 | Spirit of King Gius Abraon |
| 1871 | 1 | Spirit of Knowledge Keeper Wourha |
| 1872 | 1 | Spirit of Mechanical Engineer Ossi |
| 1873 | 1 | Spirit of Nu |
| 1874 | 1 | Spirit of Peha |
| 1875 | 1 | Spirit of Raider Captain Aganmi |
| 1876 | 1 | Spirit of Raider Etne |
| 1877 | 1 | Spirit of Raider Idpo |
| 1878 | 1 | Spirit of Raider Onbi |
| 1879 | 1 | Spirit of Raider Umpa |
| 1880 | 1 | Spirit of Ujji |
| 1881 | 1 | Spirits From Fallen Worlds |
| 1882 | 1 | Splash Hammer |
| 1883 | 1 | Stagnation Asanawa |
| 1884 | 1 | Stairwell |
| 1885 | 1 | Stake of Wodaso |
| 1886 | 1 | Stalker |
| 1887 | 1 | Standard throwing dagger from the kingdom of Bamaen.\n\nWell balanced but unremarkable, a common armament of explorers and assassins alike.  |
| 1888 | 1 | Star Charm |
| 1889 | 1 | Star Lizard |
| 1890 | 1 | Starlight Blade |
| 1891 | 1 | Start a new game on slot {index}? |
| 1892 | 1 | Starvation Asanawa |
| 1893 | 1 | Statue of a chilopoda, come to life through ancient Epha magic coursing through the Petrified Garden. |
| 1894 | 1 | Statue of a crawler, come to life through ancient Epha magic coursing through the Petrified Garden. |
| 1895 | 1 | Statue of a giant spider, come to life through ancient Epha magic coursing through the Petrified Garden. |
| 1896 | 1 | Statue of a homopiscium, come to life through ancient Epha magic coursing through the Petrified Garden.\n\nIt bears a stone crown, honoring the collaborative effort between the Epha and the Orbewa to construct the garden. |
| 1897 | 1 | Statue of a skeletal archer, come to life through ancient Epha magic coursing through the Petrified Garden. |
| 1898 | 1 | Status |
| 1899 | 1 | Status & Inventory |
| 1900 | 1 | Status Effects |
| 1901 | 1 | Steamed Clams |
| 1902 | 1 | Steel Ale |
| 1903 | 1 | Stick Deadzone |
| 1904 | 3 | Sticky |
| 1905 | 1 | Sticky Res |
| 1906 | 1 | Stock: |
| 1907 | 1 | Stone Duck Figurine |
| 1908 | 1 | Stone Duck Smasher |
| 1909 | 1 | Stone Hut |
| 1910 | 1 | Storm Gem |
| 1911 | 1 | Strange |
| 1912 | 1 | Strange Explorer |
| 1913 | 1 | Strange Silver Etched Skull |
| 1914 | 1 | Strange Stone Block |
| 1915 | 1 | Strange Voices |
| 1916 | 1 | Strange Wooden Panel |
| 1917 | 1 | Strength Charm |
| 1918 | 1 | Struck with the power of the True Holy Sword Akunra, Siraatsu's dark magical defenses have fallen. \n\nStripped of her queenly gowns, her true form has become evident. More machine than woman, she will stop at nothing to rule over this world. |
| 1919 | 1 | Sturdy Trident |
| 1920 | 1 | Stygian Barnacle |
| 1921 | 1 | Succccch stranggggge memoriessss... whomst mightttt theyyyy belong tooooo... |
| 1922 | 1 | Suddenly you happen upon a passage that you can actually understand. It's a formula for an acid which can be used to dissolve undesirable "growths of soft tissue". \n\nYou ponder if this could perhaps be used to clean up the mysterious growth that has accumulated all across the tower. |
| 1923 | 1 | Supreme Pizza |
| 1924 | 1 | Surely Irdunuub will face judgement for stealing my eggs yes.  |
| 1925 | 1 | Surely someone from the Royal Academy of Bamaen will eventually stop by to retrieve my research and I will be fondly remembered for ages to come for my great findings. Of course, I've made mention of your assistance as well. I believe it's time I moved on, someone is waiting for me on the other side, after all. |
| 1926 | 1 | Suspend Game and \n\nReturn to Desktop? |
| 1927 | 1 | Suspend Game and \n\nReturn to Title Screen? |
| 1928 | 1 | Swap Accessory {N} |
| 1929 | 1 | Swap Dagger {N} |
| 1930 | 1 | Swap Sticks |
| 1931 | 3 | Swift and deadly, this elegant leaf blade is able to harness goddess Puwema's magic in the form of spinning discs of wind and leaves. |
| 1932 | 1 | Swimmm... freeee... |
| 1933 | 1 | Sworn Protector |
| 1934 | 1 | Symbol of Jigeki |
| 1935 | 1 | T |
| 1936 | 1 | Tab |
| 1937 | 1 | Taebba, \n\nWe made our way up the cliffs above this spot and as you mentioned, there was a cache of old explosives in some barrels up there. It was really dangerous though, there were chilopodas everywhere and the pile of rocks next to the barrels came alive. We barely made it out of there alive.\n\nWe're moving camp to the Obmuer temple now. We'll await your arrival before moving down into the ruins. Look for the smoke from our fire in the early morning.\n\n- Guajja |
| 1938 | 1 | Take Photo |
| 1939 | 1 | Take account of all three hints written upon these slabs.\n\nGargoyle II has one neighbor who will take fire and one who will not but which is which? |
| 1940 | 1 | Take this ten of pentacles, a fine payment yes. |
| 1941 | 1 | Take this, it should allow you to unseal that bitch's grave. What lies beyond... well, I won't tell you. You'll have to see it for yourself.  |
| 1942 | 1 | Tap To Run |
| 1943 | 1 | Tap To Run - Gamepad |
| 1944 | 1 | Tap To Run - Keyboard / Mouse |
| 1945 | 1 | Tarnished Cell Key |
| 1946 | 1 | Tauropod |
| 1947 | 1 | Teal Green Seal Breaker |
| 1948 | 1 | Team Lead |
| 1949 | 1 | Tell Captain Aganmi about the fate of his crew. |
| 1950 | 1 | Temple Entrance |
| 1951 | 1 | Testing Lead |
| 1952 | 1 | Thank you, young prince, Gagi's spirit is fulfilled because you were able to appreciate his cooking CAW! You look quite shocked, of course I can speak CAW! He never realized you were the hero from his vision, I found this quite amusing CAW CAW! |
| 1953 | 1 | Thanks to Our Friends and Collaborators |
| 1954 | 1 | Thanks to your enchanted Dagger Belt, Throwing Weapons can be used an unlimited number of times. \n\nTo use a Throwing Weapon press {throwingweapon_binding}.\n\nOnce thrown they will take time to return to the sheath.\n\nThe amount of time it takes each Throwing Weapon to return is based on its speed attribute.\n\nCollecting multiple of the same type of Throwing Weapon will allow you to store and throw more in succession without waiting for them to return.  |
| 1955 | 1 | That book, it called to me, made me read from its macabre passages. She tried to warn me but I simply could not resist...\n\nYour Highness, take this key to the forbidden archives, perhaps your strong will can prevail where mine had failed. I feel such shame, were I able to walk I would throw myself into the ocean as penance. |
| 1956 | 1 | That creature turned me against my comrades. \n\nDaon... Imba... I can still hear their screams...\n\nI'm so sorry... |
| 1957 | 1 | That damn beast returned to finish the job while you explored the burial site... that terribly fierce beast with its gleaming red shell. |
| 1958 | 1 | That fine meal will keep your belly full and give you strength yes. |
| 1959 | 1 | That horrible meat produces such a terrible cacophony of wet noises, it's simply driving me mad. I would be quite pleased if you were able to clean up the mess that foolish knight made. |
| 1960 | 1 | That makes four. Damn! None of them survived? This isle is cursed, I was a fool to bring my crew here. |
| 1961 | 1 | That voice is fading.... can finally return to slumber... |
| 1962 | 1 | That was the last I could make, for I am all out of cheese! Come to think of it, the hero hasn't been around yet to have even one. Take this cooking pot, if you meet that hero, be sure to cook a meal for them!  |
| 1963 | 1 | That wretched Siraatsu’s army of the damned is ever increasing. |
| 1964 | 1 | The Call From Beyond |
| 1965 | 1 | The Chef Gagi is busily working away preparing a meal. He's humming to himself and seems completely unaware of your presence. Perhaps it's best to let him cook.  |
| 1966 | 1 | The Chrome Hell |
| 1967 | 1 | The Dagger Belt is required to equip Throwing Weapons. |
| 1968 | 1 | The End of Epha |
| 1969 | 1 | The Great Work IV - Rubedo |
| 1970 | 1 | The Green Goddess |
| 1971 | 1 | The History of Dragons |
| 1972 | 1 | The Holy Sword Endu consumes energy from Wind Gems embedded in the guard to perform secondary attacks.\n\nUnlike most weapons, the secondary dash attack can be performed in 4 directions using movement inputs.\n\nThe secondary attack can also be used to perform the Wind Dash when performed in the air. Wind Dash can be used to cross gaps that are too far to jump across.\n\nThe Wind Dash can also be performed regardless of equipped / in hand weapons by pressing {winddash_binding}.\n\nThere are more Wind Gems to find around the isle. The more you have, the further you can go by using successive Wi …(truncated) |
| 1973 | 1 | The Holy Sword Endu's form has warped and its power improved through bonding with Prince Beruim.\n\nIt's begun to resemble the ancient Holy Sword Akunra whose materials it was forged from.\n\nWhen swung firmly, its blade now produces an arc of holy energy. |
| 1974 | 1 | The Isle of Neasied |
| 1975 | 1 | The Kingdom of Ommu |
| 1976 | 1 | The Life of Sage Ikku |
| 1977 | 1 | The Necronomicon |
| 1978 | 1 | The New Flesh |
| 1979 | 1 | The Old Ones Beyond |
| 1980 | 1 | The Outer World |
| 1981 | 1 | The Rotting God |
| 1982 | 1 | The Royal Academy of Bamaen |
| 1983 | 1 | The Stone Duck Figurine resonates with the Crystal Monument. |
| 1984 | 1 | The Strange Explorer's cudgel, it was forged from materials even Ossi could not identify. \n\nIt emits a ball of electricity from its head through some unknown mechanism or magic. |
| 1985 | 1 | The Witch of Neasied |
| 1986 | 1 | The ancient Queen Siraatsu will soon reawaken after millennia of slumber. |
| 1987 | 1 | The bent and broken remnants of The Holy Sword Unok. Among the fragments of metal are 6 holy gems, cracked and dull and devoid of their former magic. \n\nWhen King Gius Abraon struck Queen Siraatsu with Unok, it shattered upon her inhuman breast, waking her from her ages long slumber and dooming the king. |
| 1988 | 1 | The bones of a recently deceased arbelist, it fires precise shots with its crossbow.\n\nRecently deployed to the Orbewa temple from the Bamaen mainland, negotiations broke down and they were killed during a bloody battle. |
| 1989 | 1 | The bones of a recently deceased soldier wielding a rickety shield and long reaching spear.\n\nRecently deployed to the Orbewa temple from the Bamaen mainland, negotiations broke down and they were killed during a bloody battle. |
| 1990 | 1 | The bones of a recently deceased warrior, it swings its rusted sword with reckless abandon. \n\nRecently deployed to the Orbewa temple from the Bamaen mainland, negotiations broke down and they were killed during a bloody battle. |
| 1991 | 1 | The bones of an ancient witch who refused to die even after being glassed by azure flame in the ancient war with the Muojwi.\n\nShe wanders the Forgotten Dungeon reanimating the bones of her fallen comrades while cackling madly. |
| 1992 | 1 | The book details the purpose and history of the Record of Sin and the Sin Keepers who watch over it. The Order of Sin was founded thousands of years ago after Queen Siraatsu was first defeated by Peha, the avatar of Puwema. \n\nPeha declared high technology and Dekanima magics forbidden, both of which were in common use. Instead she pushed humanity to live simpler and more grounded lives with a focus on faith in Puwema and Wodaso at its core.\n\nThe Sin Keepers were tasked with collecting and sealing away artifacts and knowledge of these forbidden subjects. They achieved what they could in the …(truncated) |
| 1993 | 1 | The bottom part of a key shaped like the symbol of Jigeki.\n\nFound in a shrine of Jigeki hidden away under the bridge to Queen Siraatsu's Castle.  |
| 1994 | 1 | The chattering skull of a venerable skeleton, ablaze with azure flame it floats through the air in a final act of defiance. |
| 1995 | 1 | The corpse of the knight Ujji, reanimated by a mysterious virus from an otherworldly creature. \n\nIn life Ujji did not accomplish much, hopefully he was a challenging foe in death. |
| 1996 | 1 | The crown of a golden puss bag that fancied itself a king.  |
| 1997 | 1 | The crown of a metal puss bag that fancied itself a king.  |
| 1998 | 1 | The crusty old wheeler who lives in the grotto, she spins her wheel of fate all day\n\nRing the bell for service was her motto, desires sated if you could pay\n\nBut now her bell isn't rung anymore, the caverns in ruins her bell inaccessible\n\nShe sits in silence a lonely whore, but still her desires remain irrepressible  |
| 1999 | 1 | The damp filthy book is filled with crazed messages written in large sloppy characters.\n\nI have been guided to the ancient crown which rightfully is making me the ruler of Orbewa! \n\nMany months pass by and still Orbewa not realize I am king. I must post royal decree so they know what do.\n\nFoolish subjects not obey my decree. The gods will surely punish them for I am true and best king in all world.\n\nSo hungry, royal subjects not offer anything to Upobna. I will send knights to punish them but.. where are knights?\n\nUpobna too weak, cannot open door to royal chamber. Will eat first per …(truncated) |
| 2000 | 1 | The dead rise from their graves to exact their revenge upon the living. |
| 2001 | 1 | The decomposing corpse of a woman with a large wheel embedded in her torso. She weaves a fateful beam of azure flame.\n\nThis vile servant of Sipeha was buried long ago. She has recently come back to life upon Queen Siraatsu's awakening. |
| 2002 | 1 | The disgusting tome coughs and several items fall from its fallow mouth. |
| 2003 | 1 | The door cannot be opened. It seems like it will open from the other side. |
| 2004 | 1 | The door cannot be opened. It seems to be held shut by some kind of mechanism.  |
| 2005 | 1 | The door remains firmly shut. The sculptures on the door must mean something.  |
| 2006 | 1 | The door was open? Oh I suppose it was. I see you're brandishing some sort of crude weapon, I suppose you need that to defend yourself out there? Since you're here, make yourself useful. There may be something in it for you as well, if I'm feeling charitable. |
| 2007 | 1 | The finest meal in all the world yes. |
| 2008 | 1 | The first and last Orbewa hatched by Erma, he smashed every remaining egg to ensure his place as the next Sea King. \n\nIt seems Jigeki's foul influence had already infiltrated the egg. |
| 2009 | 1 | The fossil of a creature that long ago walked the streets of a grand city. \n\nHolding it in your hand brings a strange A/V image into your mind.\n\nLong live the new flesh. |
| 2010 | 1 | The fossil of a mushroom thought to only exist in fairy tales and fables. \n\nIt allows you to propel yourself up through the air. |
| 2011 | 1 | The fossil of a species of anemone not seen for millennia.\n\nIt guides you out of dangerous waters before any injury can occur. |
| 2012 | 1 | The fossil of a species of fish not seen for millennia.\n\nHolding it close seems to fill your belly a bit, how strange. |
| 2013 | 1 | The fragrant and sweet juice of an iron pineapple, aged and bottled.\n\nIt has been carefully fermented until the acidic compounds of the fruit transform, increasing the potency of their skin-hardening effect. |
| 2014 | 1 | The gate is too heavy to lift. It seems to be held shut by some kind of mechanism.  |
| 2015 | 1 | The goods were being transported in my signature yellow chests, they should have arrived months ago and I've no idea where they might be. Best of luck finding them, lad!  |
| 2016 | 1 | The high priestess of a congregation dedicated to worshipping Muagyo, the avatar of Jigeki - god of rot and decay. \n\nMuagyo's status as the avatar of Jigeki has been long suppressed by the Royal Academy of Bamaen who instead continue to deify Jigeki's previous avatar, Yubior. This has allowed the congregation to easily keep secret their dark rituals which have brought forth a massive pool of rot in the swamp outside of their temple.\n\nIrdunuub and her congregation are responsible for the theft of Erma's eggs, not only stealing them but hiding them all across the land to prevent easy recover …(truncated) |
| 2017 | 1 | The inscription has no effect. The letters fade from the grimoire. |
| 2018 | 1 | The key doesn't fit this lock.  |
| 2019 | 1 | The key to the Knights of House Abraon's Expedition chests.\n\nThe knight Nu ran away with this key in a fright only to be crushed by stone from a collapsing ceiling, what an unfortunate coward. |
| 2020 | 1 | The last game console released by MEGA. |
| 2021 | 1 | The last nut of the White Tree Guardian.\n\nIt feels lucky, though you aren't quite sure why. |
| 2022 | 1 | The locked chest? Unfortunately Nu took the key with him into the mausoleum. Such bitter luck... its contents would surely be a boon right now. |
| 2023 | 1 | The lovers and the hierophant yes. A choice and a tradition. The two cannot always coexist... |
| 2024 | 1 | The machine doesn't seem to function right now. |
| 2025 | 1 | The machine produces several clicking and beeping sounds. If this was meant as communication of some kind, it's well beyond your understanding.  |
| 2026 | 1 | The middle part of a key shaped like the symbol of Jigeki.\n\nIt was fished from the gut of a gigantic serf and still carries the extremely unpleasant odor of a rotting corpse and fish bile. |
| 2027 | 1 | The moment we unsealed the door to this vault, every damn beast down here came rushing straight to us. In that chaos, that bastard Taebba grabbed the artifact and ran off! It was his idea to rig the door with explosives, did he know this would happen? \n\nI know he dreamed of the riches we might be paid for whatever artifacts we'd find here, I bet he's headed straight for the nearest merchant! I'll track that bastard down just as soon as I shake off these wounds. \n\n- Guajja |
| 2028 | 1 | The p-p-p-prince of House Abraon? We... our raiding... is only for long lost treasures, we never engage in any kind of thievery from the living and certainly not royalty. I swear it on my mother's grave! I really could use your help finding the rest of my crew... your Highness? |
| 2029 | 1 | The pen's ink has run dry, you'll need to replenish it to continue inscribing in the grimoire. |
| 2030 | 1 | The poster is crudely scrawled with near illegible handwriting.\n\nMe Upobna, royal king of all Orbewa people and also other people and everything too!\n\nHerby it is decreed you follow me and I am most greatness king for all time!\n\nBring Upobna many kingly offerings now. You bring delicious fish and berries and become honored and I am the great and royal king! |
| 2031 | 1 | The reanimated bones of a recently deceased executioner.\n\nJust before the tower was sealed, the executioner and a company of soldiers sought shelter inside from pursuing Orbewa warriors. Unfortunately they were unable to seal the front door and were killed in bloody battle right in the foyer. |
| 2032 | 1 | The ritual has failed, perhaps we'll try again in the next cycle. |
| 2033 | 1 | The shimmering blade of a kind and charming wandering ghost.\n\nWhen enough star light accumulates within, it can be discharged in an ethereal wave. |
| 2034 | 1 | The star shines in you though yes? Fate has brought you to me with purpose. You will retrieve the stolen eggs, so that I may raise a fine new Orbewa king. There are rewards in it for you as well yes. |
| 2035 | 1 | The strange being utters a few words in some incomprehensible form of speech. |
| 2036 | 1 | The top of a street lamp from another reality, used as a weapon in lands untold beneath a dark victorian city.\n\nDespite being a light fixture, it can serve as a formidable weapon, provide a bit of light, and cast burning rays over an area when its energies are charged. |
| 2037 | 1 | The top part of a key shaped like the symbol of Jigeki.\n\nIt was found in a shrine of Jigeki hidden under the Deimpo Mausoleum. \n\nEtched into the side is a small message "Yubior, I miss you."  |
| 2038 | 1 | The true form of Sipeha, goddess of the void. Not a true god but the wandering soul of a being from some distant world that died out hundreds of millions of years ago.\n\nThe soul resides in a fragment of memory recalling that world, this is perhaps the last glimpse any will ever catch of it or her long extinct race. |
| 2039 | 1 | The unearthed bones of a long-dead archer, reanimated by black magic.\n\nDespite its empty sockets, it seems to have quite a keen eye. |
| 2040 | 1 | The unearthed bones of a long-dead soldier, reanimated by black magic. Reliving battles from long ago, it searches mindlessly for prey.\n\nPerhaps it only desires to make new skeletal companions? |
| 2041 | 1 | The well-preserved bones of a highly honored knight who fought bravely in an ancient war.\n\nIt still proficiently wields its halberd and spiked buckler, as skilled as the day it died. |
| 2042 | 1 | The well-preserved bones of a mercenary who achieved great feats on the battlefields of an ancient war.\n\nAgile and aggressive, it wields two razor sharp shining silver blades.  |
| 2043 | 1 | The well-preserved bones of a skilled marksman who precisely killed enemy soldiers from afar in an ancient war.\n\nArrows fired from its elegant silver bow still strike true. |
| 2044 | 1 | There are a lot like, A LOT, of horrible bugs in the cliffs above here. I managed to scramble away to safety but some kind of like, poison got the best of me? Could you bring that amulet on my like, corpse, back to the captain? Thanks man. |
| 2045 | 1 | There are a variety of status effects that can be inflicted onto you or your enemies.\n\n<img id="Status_Poison"></> Poison - slowly drains life points.\n\n<img id="Status_Heavy"></> Heavy - slows movement and power meter regeneration.\n\n<img id="Status_Weaken"></> Weaken - increases damage taken.\n\n<img id="Status_Disease"></> Disease - decreases damage dealt.\n\n<img id="Status_Sticky"></> Sticky - prevents the use of throwing weapons.\n\n<img id="Status_Binding"></> Binding - prevents the use of magic abilities.\n\n<img id="Status_Paralysis"></> Paralysis - prevents all movement and attac …(truncated) |
| 2046 | 1 | There are many beasts to catalogue, some rarer than others. Make sure to check whatever caves you may find beneath the isle as well. This research is to be as thorough as possible. |
| 2047 | 1 | There are schematics here that should allow you to upgrade holy weapons. How fascinating that the powers of the gods could be held in such vessels. |
| 2048 | 1 | There are terminals scattered all around these isles and the caverns and lands above as well. You should be able to find data modules inside of them that would greatly speed up my research. Use this key to open them, they're marked by a very obvious symbol. You shouldn't have any trouble finding them. |
| 2049 | 1 | There are unapplied Resolution or Window Mode changes.\n\nWould you like to apply them? |
| 2050 | 1 | These notes will be quite useful as a start. Take this book, I believe it will interest you. If you were able to locate such creatures as it describes I would be quite shocked. |
| 2051 | 1 | These small piles of bright white stone come to life to keep trespassers out of the ancient world. |
| 2052 | 1 | Thief's Mask |
| 2053 | 1 | Thin Font |
| 2054 | 1 | This ancient cannon fires waves of highly charged liquid at extreme speed.\n\nIt requires energy to operate. |
| 2055 | 1 | This ancient forbidden text declares that far below the world we know, another world lies. It claims that this world below is actually the surface of a planet and that beyond it lies a great void of space with many distant planets and stars further beyond. \n\nYou don't really understand most of these concepts, it sounds quite fantastical.  |
| 2056 | 1 | This appears to be the grimoire of a witch, however its pages are all empty. \n\nIt was found alongside a writing quill which seems to quite slowly generate its own ink from within, why not inscribe something in its pages? |
| 2057 | 1 | This book describes the history of the Epha in the Kingdom of Bamaen. Though the Epha disappeared long ago, their constructions were of the highest quality and have withstood the test of time. The Epha built the Crystal Monuments and Wing Altars all around the continent and surrounding islands (including the Isle of Neasied) during the war with the Kingdom of Muojwi. It goes into some detail of a rumored garden of stone built by the Epha beneath the Isle of Neasied that can only be reached by those willing to take a leap of faith. You wonder if it could really exist.   |
| 2058 | 1 | This book has a codified set of rules to live by so that your soul may be guided to eternal paradise by the god Wodaso.\n\nYou find that very few of his followers seem to heed these rules and ponder just how empty Wodaso's eternal paradise must be. |
| 2059 | 1 | This cousin of the nautilus evolved to hunt on land, driven from the tainted waters it originated in. \n\nIt can retreat into its shell when threatened and pierces its prey with a poison-secreting bone spike. |
| 2060 | 1 | This enormous golden morning star is coated in sharp spikes and rotten residue. Its hollow interior slowly fills with a bubbling rotten liquid. \n\nThe rot within can spill out in a deadly blast when sufficiently charged. |
| 2061 | 1 | This is simply fantastic! Near as I can tell you've gathered information on every single beast on the isle. I'd like you to have this. |
| 2062 | 1 | This isle is truly damned! I've never seen such a hoard of wretched beasts before. Those eyes, always watching... |
| 2063 | 1 | This massive mechanical sword is imbued with the god Wodaso's lightning, able to unleash a devastating wave of arcing plasma. |
| 2064 | 1 | This module also contains schematics for upgrading dark weapons. Quite dreadful but use them if you must. |
| 2065 | 1 | This normal 72 year old blade is designed with sleek elegance for the efficient extraction of fruit punch.\n\nIts nimble blade can easily slash foes to ribbons or thrust neatly between their ribs. \n\nWhile holding it, you feel a mild temptation to cut off your hand and grill it. No... certainly not... <img id="B33IsNormal"></> |
| 2066 | 1 | This old silver ring is imbued with goddess Puwema's magic. Allows its wearer to hurl spinning discs of wind and leaves. |
| 2067 | 1 | This regally decorated golden spear is affixed with a rare Epha gem. King Gius kept such a weapon to symbolize the ancient union between the kingdoms of Bamaen and Epha in an age long ago. \n\nNot just for show, once charged the Epha gem can unleash a magic stone thrust. |
| 2068 | 1 | This should prove invaluable to my research. |
| 2069 | 1 | This twisted and malformed creature was once a Knowledge Keeper. Struck by ancient magic that until recently lay dormant inside statues excavated from beneath the Tower of Knowledge.\n\nIt shuffles about on all fours and gnashes its misshapen jaw at any who approach. |
| 2070 | 1 | This twisted and malformed creature was once a Knowledge Keeper. Struck by ancient magic that until recently lay dormant inside statues excavated from beneath the Tower of Knowledge.\n\nIt shuffles about on all fours, burning everything in sight with orbs of magical flame shot from the candles on its back. |
| 2071 | 1 | This twisted and malformed creature was once a grave robber. Struck by ancient magic, perhaps it was not wise to go digging through ancient tombs.\n\nIt shuffles about on all fours, burning everything in sight with orbs of azure flame shot from the candles on its back. |
| 2072 | 1 | Thorny Gape |
| 2073 | 1 | Those burning eyes, always watching. THE EYES! |
| 2074 | 1 | Though she mourned the loss of Yubior, who she loved dearly, Muagyo devoted herself to Jigeki's path and will.\n\nThird she visited the shrine tucked away below the vile goddess Puwema's temple. |
| 2075 | 1 | Throwing Axe |
| 2076 | 1 | Throwing Dagger |
| 2077 | 1 | Throwing Weapons |
| 2078 | 1 | Thumb Mouse Button 1 |
| 2079 | 1 | Thumb Mouse Button 2 |
| 2080 | 1 | Tilt |
| 2081 | 1 | To invoke the will of Okbujo, perform sacred rites of offering to the shrine. \n\nOffer mementos of a great warrior, adventurer, mother, scholar, sage, chef, artisan, and leader. |
| 2082 | 1 | To use equipment (Main weapons, Throwing Weapons, Accessories) you must select them in the status menu and equip them.\n\nSwitch between equipped Main Weapons by pressing {nextweapon_binding}.\n\nSwitch between equipped Throwing Weapons by pressing {nextthrowingweapon_binding}. |
| 2083 | 1 | Tomorrow we head into the ruins beneath Obmuer Temple to search for this supposed treasure vault. On the way over here the bridge to the main part of the isle collapsed, just how old is this place? \n\nNot even sure why I'm writing this note, just got a funny feeling I guess.\n\n- Guajja |
| 2084 | 1 | Too long have I watched over this garden of stone, finally I can rest. |
| 2085 | 1 | Total EXP |
| 2086 | 1 | Touch Pad Button |
| 2087 | 1 | Tourmaline Chromobula |
| 2088 | 1 | Tourmaline Eye Charm |
| 2089 | 1 | Tower of Knowledge |
| 2090 | 1 | Tower of Knowledge Monument Engraving |
| 2091 | 1 | Toxic Homopiscium |
| 2092 | 1 | Toxic Serf |
| 2093 | 1 | Trader Umbada |
| 2094 | 1 | Training Band |
| 2095 | 1 | Travel Crystal |
| 2096 | 1 | Travel Crystal Fragment |
| 2097 | 1 | Treacherous Baleen |
| 2098 | 1 | Treacherous Sea Prince |
| 2099 | 1 | Treasure Charm |
| 2100 | 1 | Treasure Hunter's Note I |
| 2101 | 1 | Treasure Hunter's Note II |
| 2102 | 1 | Treasure Hunter's Note III |
| 2103 | 1 | Triangular Slate |
| 2104 | 1 | Trident |
| 2105 | 1 | Trident Key - Amethyst |
| 2106 | 1 | Trident Key - Emerald |
| 2107 | 1 | Trident Key - Onyx |
| 2108 | 1 | Trident Key - Ruby |
| 2109 | 1 | Trident Key - Sapphire |
| 2110 | 1 | True Holy Sword Akunra |
| 2111 | 1 | U |
| 2112 | 1 | UPGRADED |
| 2113 | 1 | Ujji |
| 2114 | 1 | Ujji's Reanimated Corpse |
| 2115 | 1 | Umbada's Islet |
| 2116 | 1 | Umbada's Lost Merchandise Key |
| 2117 | 1 | Umbada's Trading Outpost |
| 2118 | 1 | Umbada's Trading Outpost Key |
| 2119 | 1 | Unbelieveable, I remember looking down here when a ball of water struck me from behind. I survived the fall but my spine was permanently damaged, I guess it's quite a long fall huh? If you could bring my amulet back to Aganmi for me I'd appreciate it. |
| 2120 | 1 | Unbound |
| 2121 | 1 | Underworld |
| 2122 | 1 | Unequip |
| 2123 | 1 | Ungulacrita |
| 2124 | 1 | Unholy Bone |
| 2125 | 1 | Unok's Remains |
| 2126 | 1 | Unrelated to the current situation, I am in the middle of a very important research project for the Royal Academy of Bamaen. I am to catalogue each and every kind of beast found on this isle. With things as they are I've made little progress as of late, though. Do tell me of any creatures you encounter and I'll make it worth your while, that's a promise.  |
| 2127 | 1 | Unsealed Tomb |
| 2128 | 1 | Up |
| 2129 | 1 | Up Button |
| 2130 | 1 | Up Directional Button |
| 2131 | 1 | Upgrade\n\nComplete |
| 2132 | 1 | Upgrade every Main Weapon. |
| 2133 | 1 | Upgrade {WeaponName} for {price} coins? |
| 2134 | 1 | Upim's Fairy Tales |
| 2135 | 1 | Upobna believes himself the last of the true royal line of Orbewa and has declared himself a king, though none seem too interested in following his royal decrees. |
| 2136 | 1 | Upon the passing of Yubior, the spirit of god Jigeki joined with his priestess Muagyo. She ventured across these lands in pilgrimage to the many shrines of Jigeki. Praying, partaking in devotion rituals, and leaving behind at each a fragment of the key to this temple's inner sanctum.\n\nFirst of these shrines was above in the Deimpo mausoleum, hidden beneath grinding stone. |
| 2137 | 1 | Use Item |
| 2138 | 2 | Use Items |
| 2139 | 1 | Use Throwing Weapon |
| 2140 | 1 | Use items provide a variety of useful effects such as healing, curing status effects, and more.\n\nTo use them, you can select them from the status menu or you can activate them during gameplay from the quick slot by pressing {useitem_binding}.\n\nSwitch the item in the quick slot by pressing {nextitem_binding} and {previousitem_binding}.\n\nItems can be added or removed from the quick slots by selecting them in the status menu.  |
| 2141 | 1 | Usunha & Wujomiod, Shrine of Puwema Gatekeepers |
| 2142 | 1 | V |
| 2143 | 1 | VSync |
| 2144 | 1 | Vampire's Mirror |
| 2145 | 1 | Vampiric Blade |
| 2146 | 1 | Vampiric Scimitar |
| 2147 | 1 | Velvet Key Stone |
| 2148 | 1 | Venerable Skeletal Knight |
| 2149 | 1 | Venerable Skeletal Marksman |
| 2150 | 1 | Venerable Skeletal Mercenary |
| 2151 | 1 | Ventilation Shaft |
| 2152 | 1 | Verdant Hollow |
| 2153 | 1 | Vermilion Homopiscium |
| 2154 | 1 | Versatile Volley |
| 2155 | 1 | Version {Number} |
| 2156 | 1 | Very Fast |
| 2157 | 1 | Very Slow |
| 2158 | 1 | Vespula Ungulacrita |
| 2159 | 1 | Vibrant Wind Gem |
| 2160 | 1 | Vibration Intensity |
| 2161 | 1 | Video |
| 2162 | 1 | Video Options |
| 2163 | 1 | View Button |
| 2164 | 1 | Vigilant Keeper |
| 2165 | 1 | Vigor Lamp |
| 2166 | 1 | Vile Serf |
| 2167 | 1 | Vineshroom |
| 2168 | 1 | Virtua Nail-Bat |
| 2169 | 1 | Virtual |
| 2170 | 1 | Viscous Thorny Gape |
| 2171 | 1 | Vomit |
| 2172 | 1 | Voxord |
| 2173 | 1 | W |
| 2174 | 1 | Wake From Dreams |
| 2175 | 1 | Wake the Petrified Garden. |
| 2176 | 1 | Wami is one of three members of the Royal Orbewa Guard, sworn to protect Dagoom the Sea King.\n\nShe bears a silver scallop tiara, given to her by the Sea King during the Royal Orbewa Guard ceremony. The blessings of this ceremony permanently altered her coloration and bestowed her with ancient Orbewa water magic.  |
| 2177 | 1 | Wami of the Royal Orbewa Guard |
| 2178 | 1 | Wanderers of the Eastern Continent |
| 2179 | 1 | Wandering Stalker |
| 2180 | 1 | Warding Amulet |
| 2181 | 1 | Wave Gun |
| 2182 | 1 | Waxy Loot Sack |
| 2183 | 1 | We mussssst prepare... rituallll sooooon... |
| 2184 | 1 | We spent months painstakingly cleaning and restoring the stone model city found below, only to be informed that it is not suitable for display and must be hidden away in storage.\n\nI just had the placard engraved and affixed, it was ready for showtime! It would have been a grand decoration in the tower's foyer, what an utter disappointment! |
| 2185 | 1 | We're at the deepest sanctum of the temple now that isn't wholly flooded. We've prepared a boat to travel further in from here. Piodor insists we will find Yubior's grave and more in the cavern beyond this point. The man we hired to help carry supplies has overheard some of Piodor's theories and seems quite agitated. He insists that we will find nothing here that proves such blasphemy. We shall see I suppose. |
| 2186 | 1 | We've arrived at the sunken Obmuer Temple, first home of the Orbewa people. My friend Piodor insists that we will find evidence within the sunken chambers beyond that proves a radical theory. He claims the current avatar of Jigeki is no longer Yubior but is, in fact, of the Orbewa People. This theory would be seen as heresy by the Royal Academy of Bamaen unless we offer solid proof. I know not why this matters so much to Piodor but I admit it would be a thrilling discovery were it true. |
| 2187 | 1 | Weak |
| 2188 | 1 | Weaken |
| 2189 | 1 | Weaken Res |
| 2190 | 1 | Weapon Broken |
| 2191 | 1 | Weapon Evolved |
| 2192 | 1 | Weapon Level Up |
| 2193 | 1 | Welcome to my kitchen! Have we met somewhere before? I think we have, we must have!  |
| 2194 | 1 | Welcome to the Tower of Knowledge, erected many generations ago by the Royal Academy of Bamaen for the purpose of research and archiving of knowledge on this historically significant Isle of Neasied. |
| 2195 | 1 | Well Travelled |
| 2196 | 1 | Well if it isn't a new customer! I'm Umbada, trader of the finest wares! If you don't mind my asking, who might you be, young lad?  |
| 2197 | 1 | What can I do for you today, my friend? |
| 2198 | 1 | What in the world? Young lad, where have you been adventuring to have found such a duck? As thanks, I'll make some new wares available to purchase.  |
| 2199 | 1 | What's this? You've found a duck figurine? Give it here, lad! |
| 2200 | 1 | What's wrong, did you not fancy making this prison your home for eternity? |
| 2201 | 1 | Wheel Dancer |
| 2202 | 1 | When affixed to a Mask of Second Sight, allows the wearer to see magics and runes of Jigeki.\n\nGod Jigeki, the hand of decay, brings about plague, rot, and death.\n\nCommonly depicted as his current avatar Yubior, who rides atop a giant serpent. Yubior ventured deep into Obmuer temple several years ago and has not been seen since.  |
| 2203 | 1 | When affixed to a Mask of Second Sight, allows the wearer to see magics and runes of Puwema.\n\nGoddess Puwema, the pure mother of all, bestows blessings of nature and fertility. \n\nShe is depicted by her ancient avatar Ruunen, holding a potted tree and adorned with a winged tiara. Ruunen is a figure from ancient history, more recent avatars of Puwema have not been memorialized as of yet.  |
| 2204 | 1 | When affixed to a Mask of Second Sight, allows the wearer to see magics and runes of Sipeha.\n\nGoddess Sipeha, the woman of the void, takes until all that remains is cold dark emptiness. \n\nShe is depicted as her avatar, Queen Siraatsu. Despite passing long ago, a new avatar has yet to emerge. Current events suggest this may be due to Siraatsu only entering a hibernation-like state rather than actually being dead. |
| 2205 | 1 | When affixed to a Mask of Second Sight, allows the wearer to see magics and runes of Wodaso.\n\nGod Wodaso, the great mind of power, metes out boons of energy and wisdom.\n\nHe is depicted as a man holding a scroll. The identity of this man is unknown and all records of avatars of Wodaso seem to have been lost or forgotten.  |
| 2206 | 1 | White Quartz Charm |
| 2207 | 1 | White Tree Guardian |
| 2208 | 1 | Who are you, how did you get into my lab? I'm very busy with my research, get out at once! |
| 2209 | 1 | Who... who's there? My sight was stolen by the claw of a dreadful and near imperceptible creature. I can still hear its boney feet scraping the stone floor beneath us now. Whoever you are, you must be careful! |
| 2210 | 1 | Why would you eat this? <img id="B33IsNormal"></> |
| 2211 | 1 | Wields troublesome astral weapon. |
| 2212 | 1 | Wild Ritudeon |
| 2213 | 1 | Wind Blaster |
| 2214 | 1 | Wind Dash |
| 2215 | 1 | Wind Gem |
| 2216 | 1 | Wind Gems of the True Holy Sword Akunra. They swirl within the sword's cage as if alive.  |
| 2217 | 1 | Wind Rapier |
| 2218 | 1 | Window Mode |
| 2219 | 1 | Windowed |
| 2220 | 1 | Wing Altar Travel |
| 2221 | 1 | Wing Altar Undiscovered |
| 2222 | 1 | Wing Altars |
| 2223 | 1 | Wing Stone |
| 2224 | 1 | Wing Stone Fragment |
| 2225 | 1 | Wingcast Game System |
| 2226 | 1 | Wise Work |
| 2227 | 1 | Witch's Grimoire |
| 2228 | 1 | Witch's Scythe |
| 2229 | 1 | Witchcraft |
| 2230 | 1 | With Queen Siraatsu defeated, peace has been restored to the land of Bamaen and beyond. However, it's only a matter of time before Sipeha joins with a new avatar. The fight against the evil goddess of the void may never end... |
| 2231 | 1 | With Sipeha slain, peace may well reign eternal over the world. Prince Beruim will forever be remembered as the slayer of the dark goddess.  |
| 2232 | 1 | With her imminent awakening beasts are going mad with rage and lusting for blood. |
| 2233 | 1 | With the Sin Keepers absence, these pests have taken up residence in many of the Record of Sin's tomes.  |
| 2234 | 1 | With this you can slow the beasts to a near crawl and take ever more thorough notes. You've been taking the notes after you kill them? You've been killing these beasts? Goodness I had never imagined.... |
| 2235 | 1 | Within this house of sacred remains\n\nThose who fought valiantly rest eternal\n\nGreat God Wodaso watches over\n\nGuiding the souls to eternal paradise\n\nGoddess Puwema takes root below\n\nReturning earthly remains to the sacred cycle\n\nTogether they seal away the dark\n\nLet the dead rest peacefully |
| 2236 | 1 | Witness the result of Mechanical Engineer Ossi's experiment. |
| 2237 | 1 | Wodaso |
| 2238 | 1 | Wodaso's Embrace |
| 2239 | 1 | Wodaso's Exile |
| 2240 | 1 | Wodaso's Guidance |
| 2241 | 1 | Wodaso's Holy Lightning |
| 2242 | 1 | Wodaso's Lens |
| 2243 | 1 | Wodaso's Power Cube |
| 2244 | 1 | Wodaso's Temple Keeper |
| 2245 | 1 | Wodaso's shrine seems devoid of his protection, all manner of horrors roam freely. Worse yet, the gate is shut firm, I can hear the knights not far beyond but they do not seem to be faring any better. All of our group of pilgrims but myself have been crushed by living stones and burned by balls of azure flame launched from somewhere beyond the shadows. \n\nI will remain hidden here. I would rather starve than face such a gruesome fate. |
| 2246 | 1 | Wonderful, you've located all of the missing data modules. This should be everything I need to revive this strange being. Just think of everything we'll learn from it! |
| 2247 | 1 | Wooden Club |
| 2248 | 1 | Work Log 11-4 night\n\nWe've been down here for nearly a week at this point. Structurally things seem quite stable which is very fortunate. Since no structural reinforcements were necessary, our time down here has instead been spent excavating the strange relics we keep finding and sending them above for study. |
| 2249 | 1 | Work Log 12-4 afternoon\n\nWe finally managed to get into this gated room earlier in the morning. Found a really gross old book bound in what seems to be the skin of a human face. The chamber beyond leads to some incredibly ancient structures even deeper beneath the tower. Some of the men went in there and haven't come back yet, think we're gonna have to back out of this job. It's not really our field of expertise and if I'm being honest, it creeps me out working down here. |
| 2250 | 1 | Work Log 6-4 morning\n\nKnowledge Keeper Inha reported some loose boards in the stairwell. Upon further inspection there seems to be some kind of cavern beneath the tower. I've gathered a crew and tomorrow we're going to make our way down there and make sure the tower is structurally sound. It would be a disaster if such a historical structure were to simply collapse into the sea. |
| 2251 | 1 | Wourha's Memento |
| 2252 | 1 | Wourha's Study |
| 2253 | 1 | Woyeki wields a massive beam cannon with deadly precision. \n\nInside the chamber where they've been imprisoned, they continue to hone their fighting skill. Slaughtering each other and resurrecting, growing more insane with each death. |
| 2254 | 1 | Wraith Blade |
| 2255 | 1 | X |
| 2256 | 3 | Xbox |
| 2257 | 1 | Y |
| 2258 | 3 | Yeeeeeeeeeee\n\nNo one is going to get this reference. |
| 2259 | 1 | Yepo is one of three members of the Royal Orbewa Guard, sworn to protect Dagoom the Sea King.\n\nShe bears a silver conch necklace, given to her by the Sea King during the Royal Orbewa Guard ceremony. The blessings of this ceremony permanently altered her coloration and bestowed her with ancient Orbewa water magic.  |
| 2260 | 1 | Yepo of the Royal Orbewa Guard |
| 2261 | 1 | Yessss... Yesssss! Memoriessss can be puttt in place... the ritualll mayyy beginnnn sooooon... |
| 2262 | 1 | Yet months have passed without receiving word from his majesty. |
| 2263 | 1 | You are still as the fool, only beginning yes.  |
| 2264 | 1 | You awaken having glimpsed all possible futures. |
| 2265 | 1 | You awaken having glimpsed but one possible future.  |
| 2266 | 1 | You begin to flip through the strange, stinky old book. It's impossible to make heads or tails of the bizarre symbols and illustrations that line its pages.\n\nThe more you look, the weirder you feel. Your skin ripples with goosebumps and cold beads of sweat begin to run down the back of your neck. |
| 2267 | 1 | You can also use that key to access the buildings across these isles. Now hurry along, I have research to return to and you've already spent enough of my precious time. |
| 2268 | 1 | You can call it Bo or you can call it Bladely. \n\nDo not call it Bob.\n\nThe magic gem embedded within allows you to unleash a radiant blast of homie love, sure to brighten your day. |
| 2269 | 1 | You changed your perspective like the hanged man and found more eggs yes. |
| 2270 | 1 | You don't have anything that seems to fit.  |
| 2271 | 1 | You don't have anything that you could put here. |
| 2272 | 1 | You don't have enough coins to purchase this. |
| 2273 | 1 | You don't have enough coins to upgrade this. |
| 2274 | 1 | You impudent mortal roach! HOW DARE YOU! |
| 2275 | 1 | You must ascertain which pair of gargoyles will take fire and open the way.\n\nGargoyle III is not like those to each of his sides, but do his neighbors take fire or does he? |
| 2276 | 1 | You must succeed where your father has failed. It won't be easy, I'll tell you that much. You must seek the spaces inbetween, find the Shrine of Puwema and do what you must there. That is your only hope. |
| 2277 | 1 | You reap what you sow.  |
| 2278 | 1 | You see, I could use more data to understand this mysterious being that I found floating in the waters below. Its suit appears quite advanced and I wish to revive it in order to learn more from it. |
| 2279 | 1 | You thought me defeated? HA! Your faith is as meager as your pathetic father's was! And that sickly half of a holy sword? A frail joke, forged by pathetic human hands.\n\nNow die. |
| 2280 | 1 | You're stronger than you look. Still you're no match for Queen Siraatsu. Turn back while you still can, whelp.  |
| 2281 | 1 | You've brought death to that king of swords yes. I will raise a fine new prince to take his place.  |
| 2282 | 1 | You've certainly located quite a few beasts that I'd not even suspected the existence of. Now that I'm aware you've actually been killing these beasts, I've retrieved a weapon that you may find of use from storage. |
| 2283 | 1 | You've done me a great service finding my knights your Highness. We'll keep watch over you from beyond. Surely Wodaso will grant us that.  |
| 2284 | 1 | You've found a data module? Give it here. |
| 2285 | 1 | You've found quite a few meal tickets now, perhaps more than a few were left behind by that hero I sense roaming about the isle? I've cooked up something special, I suppose you can have it in the hero's stead, seeing as you're here and they aren't. |
| 2286 | 1 | You've glimpsed past the moon's veil and found many eggs yes. |
| 2287 | 1 | You've located more creatures? Wonderful, show me your notes. |
| 2288 | 1 | Your Highness! Puwema's blessings upon us. I have prayed day and night that aid might come and here you are! For a moment I thought Ippibi or Budaen had returned from their errands.  |
| 2289 | 1 | Your Highness, I cannot move on without my company of knights, every last one of them. We're brothers in arms, we'll venture boldly into the afterlife together. |
| 2290 | 1 | Your Highness, I did not expect to see you here on this accursed Isle. I was injured attempting entry to this shrine. A damn beast snuck up on me! |
| 2291 | 1 | Your bond with Endu is not strong enough. |
| 2292 | 1 | Your father King Gius Abraon journeyed to the queen’s resting place on the Isle of Neasied. |
| 2293 | 1 | Your patronage has truly gone above and beyond my grandest hopes! There are a few select goods that were lost in transport. If you find them I'd like you to take them for yourself, free of charge. You'll need this key to get at them, though. |
| 2294 | 1 | Your temperance is unmatched yes. Patiently you seek out each egg. |
| 2295 | 1 | Youuuu havvvvve foundddd themmm... Silver Etched Skullsssss?  |
| 2296 | 1 | Youuuuu... princcccce. Bring usssss Silver Etched Skulls. Memorieeees... |
| 2297 | 1 | Yuumed swings her two pronged greatsword with immeasurable force. \n\nImprisoned by Siraatsu because they proved impossible to control, the Blood Angel Lords never-ending lust for blood and battle cannot be sated. |
| 2298 | 1 | Z |
| 2299 | 1 | ZL |
| 2300 | 1 | ZR |
| 2301 | 1 | Zerkhander |
| 2302 | 1 | Zoom |
| 2303 | 1 | [ |
| 2304 | 1 | \ |
| 2305 | 1 | ] |
| 2306 | 1 | ` |
| 2307 | 1 | and rotten minds |
| 2308 | 1 | consort of sycophants |
| 2309 | 1 | distorted decrees |
| 2310 | 1 | eat |
| 2311 | 1 | false proclamations |
| 2312 | 1 | fool's perception |
| 2313 | 1 | from decayed hearts |
| 2314 | 1 | gotttta goooo.... faaasssssstttt... |
| 2315 | 1 | living a lie |
| 2316 | 1 | of parrot sages |
| 2317 | 1 | on thrones of delusion |
| 2318 | 1 | perverted wisdom |
| 2319 | 1 | poison tongues |
| 2320 | 1 | projected fears |
| 2321 | 1 | stolen accolades |
| 2322 | 1 | turned a blind eye |
| 2323 | 1 | whore's conviction |
| 2324 | 1 | with barren emotion |
| 2325 | 1 | {Hours}h{Minutes}m{Seconds}s |
| 2326 | 1 | {Num} Ancient Coins |
| 2327 | 1 | {num}% |
| 2328 | 1 | Debug Options |

## Not recovered: the key -> text pairing

The strings are certain (they are the file's actual string table, byte-exact). What is
**not** recovered is which localisation key points at which string. The records before the string
table hold `(hash, name)` pairs but the field that carries the string index did not align under any
of the struct shapes tried (`u32+FString`, `u32+u32+FString`, `u32+FString+i32`,
`FString+count+(key)*`, flat and nested), all of which were required to tile exactly to
`string_table_offset` and none did. The raw region is `[0x1D, 85689)` of the file.

The smallest remaining step: dump that region with a hex editor alongside
`GetDataTableRow`-free UE source for this build's locres writer
(`FTextLocalizationResource::SaveToStream`), or diff against a stock UE 4.27 locres produced by
the same engine version to see the added field. Everything else in the file is solved.

Practical impact: **low**. Text is the deliverable and it is complete; keys here are mostly
`binding_*`, `numformat_*`, `demosplash_*` plus the 32-hex imported GUIDs, so a key->text map would
add ordering and context but almost no new text.


## What is and is not in this file

Present (representative, from the catalogue above):

* **item descriptions** — the long `A …` entries: *"A blood red key forged in the style of Josami
  armor."*, *"A diamond shaped metal plate with a glass sword embedded in its center. Unseals sword
  doors in Siraatsu Castle."*, *"A frail ancient short sword capable of a secret technique few know
  of."*
* **journal / readable text** — dated diary entries (`19-4`, `2-7`, `4-26`, `5-22`, …) with full
  multi-paragraph lore, e.g. the Sin Keeper's account of the silver egg from the Dekanima, and the
  pilgrim's record of the Deimpo Mausoleum.
* **quest text** — `"Find the two keys. One is at the Tower of Knowledge."`,
  `"Get key from librarian"`, `"Here take this spooky item"`, `"fine take this key"`.
* **UI** — `"{Hours}h{Minutes}m{Seconds}s"`, `"{Num} Ancient Coins"`, `"{num}%"`,
  `"Debug Options"`, `"Reset Achievements"`, `"Cheat Stick"`.
* **debug/telemetry strings that leak the architecture** — `"Updating Quest State From {cur} to
  {next}"`, `"{OBJECT}" Attempted to Activate a null pointer in Activation list Index "{index}"
  THIS IS A BUG! Please report!!`, `"There are unapplied Resolution or Window Mode changes."`.
* **credits** — `"Eric Berger [Biogenesis] & Bradley Boothe [Birdlay]"`,
  `"Freshly Baked Games LLC"`.

**Not present: spoken dialogue lines / subtitles.** 2,329 strings for a ~10 hour game means the
voice-over has no subtitles in this resource. Confirmed independently two ways: the pak contains 41
`.locres` files, of which 9 are `Game` in different languages and the other 32 are
`Engine`/`OnlineSubsystem*`; and a whole-pak byte scan for `Subtitle` returns 15 hits, all in
`Engine/Config/BaseEngine.ini` and `Engine.locres` — no game asset mentions subtitles at all (see
`datatables.md`: there are no DataTables anywhere either, so there is no dialogue-line table).

Also absent: **monster and item display names**. The weapons are named only by asset
(`RustySword_Item`, `BasicSpear`, …), not by a localised string, so the game renders their names
from asset names or from `*_Data` assets rather than from `Game.locres`.


## Appendix — every human-readable key name found in the record region (2338)

These are the FString names in `[0x1D, 0x14EB9)` that are not 32-hex GUIDs and not asset paths. They are the game's localisation key namespace, and they are the most direct evidence of how the writers organised text.

```
Dialogue  ImportedStringTable  demosplash_gingerquote  demosplash_lillyquote
binding_apostrophe  binding_ns_spb_right  numformat_pluspercent  binding_comma
binding_hyphen  binding_ns_spb_left  binding_period  binding_slash
inha_j_01_desc  cas_servnote_2_desc  sinkp_jrnl_desc  maus_pilgrimnote_1_desc
inha_j_03_desc  frst_jigekishrine_desc  swmp_bigfish_desc  inha_j_02_desc
swmp_horn_jrnl_1_desc  maus_pilgrimnote_2_desc  swmp_cultnote_desc  binding_semicolon
binding_ps_fbright  binding_ps_fbbottom  binding_ps_fbleft  binding_ps_fbtop
binding_equals  au_droptunnel_areaname  unknownval  binding_a
binding_ns_fbright  binding_xb_fbbottom  josami_deserter_2_desc  josami_deserter_1_desc
josami_deserter_4_desc  josami_deserter_3_desc  cha_quest_shop_unlocks_name  credits_1
cha_endings_2_name  cha_quest_lib_name  acc_allresists_desc  drillrobot_ca_desc
ribber_gr_desc  ribber_aq_desc  libbook0005_desc  sinbook0009_desc
wep_hempie_sword_desc  grave_warden_desc  key_josamired_desc  crawler_sz_desc
book_cook_desc  libbook0012_desc  book_jigeki_desc  book_dragon_desc
sinbook0003_desc  book_fairytales_desc  book_eldritch_desc  libbook0009_desc
book_sipeha_desc  book_rattybug_desc  book_war_desc  item_rejuv_potion_desc
acc_caputring_desc  trs_gcrab_desc  trs_josamiskull_desc  key_rosorb_desc
green_dragon_desc  slime_sz_desc  slime_ca_desc  slime_au_desc
wep_snipergun_desc  fishman_aq_2_desc  key_fishmaidentiara_desc  trs_rotchalice_desc
trs_rotchalice2_desc  runecharm_sci_desc  runecharm_food_desc  runecharm_egg_desc
runecharm_lizer_desc  runecharm_skull_desc  runecharm_chest_desc  cocker_sz_desc
trs_jigeki_citrine_desc  wep_jarhammer_desc  looker_au_desc  wep_woodclub_evo_desc
wep_woodclub_shop_desc  wep_woodclub_desc  acidvial_addeddesc_desc  wep_simpleaxe_desc
trw_light_desc  weapon_upgrade_unlock_7_desc  wep_rustyspear_shop_desc  wep_rustyspear_desc
upgrade_energycap_half_desc  trs_mecharm_desc  wep_cyberhammer_desc  weapon_upgrade_unlock_5_desc
acc_xp_1_desc  d_erma_allegg_04  acc_fishwoman_reward_desc  item_tp_crystal_desc
trs_ritualskull2_desc  wep_harpo_club_desc  item_cure_potion_desc  weapon_upgrade_unlock_4_desc
trs_holybone2_desc  deer_au_desc  sinbook0000_desc  libbook0007_desc
key_towerdoor_desc  key_castle_cup_desc  key_castle_pentacle_desc  key_castle_sword_desc
key_castle_wand_desc  key_forbiddendoor_desc  key_officedoor_desc  key_sz_door_red_desc
key_sz_door_green_desc  key_sz_door_blue_desc  upgrade_healing_desc  trs_monocle_desc
manticore_lib_desc  stalker_sz_desc  key_shopupgrade_4_desc  key_shopupgrade_1_desc
key_shopupgrade_3_desc  trs_josamiskull2_desc  lizer_sz_desc  lizer_gr_desc
lizer_ma_desc  lizer_aq_desc  lizer_au_desc  lizer_lb_desc
lizer_ca_desc  lizer_bomb_sz_desc  wep_byrdseede_sword_desc  d_wourha_r_02
d_umbada_giveduck_1_01  wep_rapier_desc  key_daggers_unlock_desc  upgrade_damage_desc
item_berserk_potion_desc  key_shopupgrade_2_desc  nude_gargler_desc  big_ballooner_desc
ballooner_sz_desc  manta_sz_desc  manta_ca_desc  drgfly_gr_desc
looker_sz_desc  mknight_sz_desc  roof_mknight_desc  trs_holybone_desc
wep_pandorablade_desc  wep_pebblehammer_desc  key_shopupgrade_0_desc  wep_kinghammer_desc
hellslime_disease_desc  wep_rustyspear_evo_desc  seaslug_big_desc  trs_royalfish_gold_desc
trs_royalchicken_gold_desc  trs_royalribber_gold_desc  trs_gold_ratty_desc  trs_jewelfish_gold_desc
acc_kingscrest_desc  trs_sipeha_gold_desc  trs_puwema_gold_desc  trs_jigeki_gold_desc
trs_wodaso_gold_desc  trs_gold_lizer_desc  wep_vampirehammer_desc  trs_fancybell2_desc
wep_tridentkeytrident_desc  key_merchant_chest_desc  d_erma_gavereward_03  trs_genielamp2_desc
trs_genielamp_desc  acc_regen_desc  trs_gold_wodasocross_desc  wep_magictorch_desc
acc_jaybook_desc  wep_voxord_desc  wep_saurianwing_desc  wep_jarhammer_shop_desc
item_glowfruit_desc  wep_magicheavysword_desc  wep_detestableclub_desc  black_drillrobot_desc
black_bigrobot_desc  trw_tech_dagger_desc  libbook0006_desc  sinbook0008_desc
libbook0004_desc  d_erma_gavereward_05  weapon_upgrade_unlock_1_desc  mimic_gr_desc
mimic_sz_desc  mimic_red_desc  item_heal_potion_3_desc  big_spiker_desc
trs_puwema_jade_desc  trs_jar2_desc  trs_jar_desc  wep_sipeha_hammer_desc
wep_katar_desc  bigrobot_ca_desc  barnacle_aq_desc  barnacle_sz_desc
item_heal_potion_2_desc  acc_attack_1_desc  acc_defense_1_desc  bookbug_lb_desc
wep_yusk_club_desc  item_resist_mp_potion_desc  manticore_au_desc  iron_lizer_desc
berserk_lizer_desc  wep_challengereward_desc  trw_vineshroom_desc  wep_ow_spikedclub_desc
wep_plasmagun_desc  wep_manroomcross_desc  wep_ow_spear_desc  white_tgolem_desc
wep_elecgun_desc  wep_skullrod_desc  wk_fish_desc  weapon_upgrade_unlock_2_desc
wep_cookingpot_desc  d_erma_reward_02  d_erma_reward_03  deer_sz_desc
spiker_ma_desc  manticore_sz_desc  acc_verhomask_desc  wep_rusted_greataxe_shop_desc
wep_rusted_greataxe_desc  wep_flameaxe_desc  wep_rusted_greatsword_shop_desc  wep_rusted_greatsword_desc
trw_grenade_desc  gargler_sz_2_desc  gargler_sz_desc  spider_sz_desc
fishman_aq_3_desc  item_inf_tp_crystal_desc  trs_ritualskull_desc  fishman_sz_desc
trw_throwingduck_desc  trs_mirror2_desc  weapon_upgrade_unlock_10_desc  tp_desc_sg
sg_hintpedestal_4  sg_hintpedestal_2  sg_hintpedestal_3  sg_hintpedestal_1
sg_hintpedestal_5  magic_chickilus_desc  wep_golemarm_desc  acc_raider_desc
d_aganmi_gavenecklace_01  wep_needlerapier_desc  key_foodticket_desc  item_inf_returngem_desc
trw_para_dagger_desc  sparkle_bug_desc  wep_slashgun_desc  golem_sz_2_desc
wep_lunaspear_desc  trw_heavy_dagger_desc  d_umbada_01  seaslug_sz_desc
trs_wodaso_quartz_desc  acc_energyrate_desc  item_wxp_orb_desc  item_cure_disease_desc
ratty_gold_desc  wep_vampirescimitar_desc  yellowjacket_deer_desc  d_en_00
ratty_gr_desc  seaslug_aq_desc  ratty_au_desc  sinbook0005_desc
item_cure_heavy_desc  wep_rustysword_desc  mossy_mknight_desc  wep_luude_crossbow_desc
wep_flingsword_shop_desc  fish_sz_2_desc  weapon_upgrade_unlock_9_desc  rusted_mknight_desc
canrobot_au_desc  drillrobot_au_desc  flyrobot_au_desc  bigrobot_au_desc
black_skull_desc  wep_scythe_desc  flyrobot_ca_desc  ghost_fish_desc
magic_cuttledog_desc  giant_fish_desc  libbook0011_desc  necro_zombie_sz_desc
necro_zombie_desc  wep_lightningzwei_desc  wep_rusted_hammer_desc  wep_rusted_hammer_shop_desc
trs_fancybell_desc  key_merchant_door_desc  trs_silver_ratty_desc  trs_jewelfish_silver_desc
acc_knight_desc  trs_sipeha_silver_desc  trs_puwema_silver_desc  trs_jigeki_silver_desc
trs_wodaso_silver_desc  trs_silver_jewel_lizer_desc  looker_dek_desc  key_brasschest_desc
acc_trw_cooldown_desc  upgrade_health_desc  key_secretskull_desc  wep_ghostspear_desc
key_grimoireink_desc  item_returngem_desc  trs_cellphone_desc  acc_defense_desc
trs_pendant_desc  acc_diseaseresist_desc  acc_bindresist_desc  acc_heavyresist_desc
acc_poisonresist_desc  acc_weakenresist_desc  acc_stickyresist_desc  trs_lizer_2_desc
trs_lizer_5_desc  trs_lizer_7_desc  trs_lizer_4_desc  trs_lizer_3_desc
trs_lizer_6_desc  trs_lizer_1_desc  ancientcity_plac_desc  trs_pendant2_desc
trs_rattyplush_desc  item_heal_potion_desc  acc_hp_desc  wep_magicspear_desc
drgfly_swamp_desc  drgfly_aq_desc  blood_stalker_desc  wep_cybersword_desc
wep_morningstar_desc  wep_duckhammer_desc  monacle_gargler_desc  gargler_lb_2_desc
gargler_lb_desc  spiker_ca_desc  wheeler_ca_desc  acc_xp_desc
item_iron_fruit_desc  weapon_upgrade_unlock_8_desc  bone_machine_desc  cuttledog_au_desc
acc_windcrystalartifact_desc  wep_cestus_desc  wep_tridentkeytrident_shop_desc  wep_spectralsword_desc
trs_jarhammer_treasure_desc  blood_bullsnail_desc  book_alchemy_desc  trw_homing_dagger_desc
wep_claw_desc  wep_rusted_morningstar_shop_desc  wep_rocketlauncher_desc  orbiter_desc
wep_crushinglance_desc  wep_rusted_morningstar_desc  wep_greatspear_desc  key_tridentkey_4_desc
key_tridentkey_0_desc  key_tridentkey_1_desc  key_tridentkey_3_desc  key_tridentkey_2_desc
key_tridentkey_5_desc  sinbook0004_desc  trw_vampire_dagger_desc  wep_magicalmartini_scythe_desc
libbook0010_desc  gold_slime_desc  silver_slime_desc  key_acidvial_unlock_desc
wep_zerkhander_desc  wep_magicwarhammer_desc  trw_axe_desc  fated_wheeler_desc
key_swordshrine_desc  teleport_title  credits_accountmanager  photofilter_acid
key_acidvial_unlock_name  cha_challenge_allteleporters_desc  modal_addtoquickslots  credits_4
flyrobot_ca_name  intro_line_01  aqr_save_raidercamp  memento_raidercaptain_name
acc_raider_name  acc_bindresist_name  slime_ca_name  golem_ma_desc
d_scientist_gavekey_07  d_erma_00  d_scientist_gavekey_05  d_erma_gaveegg_02
d_peha_00  d_aganmi_00  acc_akumuamulet_name  end1text_01
bookbug_albino_name  lib0007_desc  d_gagi_01  pausemenu_quitwarning_nosave
pausemenu_quitwarning  d_aganmi_allnecklace_03  options_crosshairmode_alwayshide  options_crosshairmode_alwayson
acc_poisonresist_name  acc_dropluck_name  acc_dropluck_desc  trs_sipeha_onyx_desc
wep_hempiesvt38_desc  wep_eyepea_syringe_desc  acc_wepxp_desc  bullsnail_sz_desc
acc_akumuamulet_desc  acc_chilldogamulet_desc  acc_fish_desc  gold_drillrobot_desc
wep_finalclaw_desc  wep_heavyriflegun_desc  item_mementomirror_desc  fish_au_desc
fish_aq_desc  trs_bottle_desc  wep_rusted_leafblade_desc  wep_rusted_leafblade_shop_desc
weapon_upgrade_unlock_3_desc  upgrade_energycap_desc  trs_bottle2_desc  wep_aquahammer_desc
necronomicon_desc  book_bonemachine_desc  trs_bottle3_desc  moss_golem_desc
mknight_ca_desc  canrobot_ca_desc  fishman_aq_desc  acc_magicpoisonresist_desc
josami_sz_2_desc  josami_sz_desc  wep_halberd_desc  bat_big_desc
wep_diamondblade_desc  memento_fishwife_desc  memento_chefandcrow_desc  memento_knight_desc
memento_king_desc  memento_librarian_desc  memento_scientist_desc  memento_raidercaptain_desc
memento_bmmc_desc  weapon_upgrade_unlock_6_desc  evil_queen_desc  bookbug_lb_2_desc
acc_librarian_reward_desc  wep_spikeshield_desc  wep_energyspear_desc  wep_energysword_desc
trw_energydagger_desc  book_dams_desc  bookbug_albino_desc  book_gaystuff_desc
lordly_spiker_desc  acc_attack_desc  trs_mirror_desc  trs_magicwatch2_desc
trw_disease_dagger_desc  trs_magicwatch_desc  lib_woodpanel_desc  trs_compass2_desc
trs_compass_desc  acc_runecharm_desc  wander_stalker_desc  trw_ow_weaken_dagger_desc
trw_magicfire_dagger_desc  wep_bird_flail_desc  key_fishegg_desc  bat_ma_desc
bat_sz_desc  spider_ma_desc  item_cure_binding_desc  trw_stone_block_desc
trw_shop_stone_block_desc  option_controls_analogsticks  flyrobot_au_name  bigrobot_au_name
coin_name  weapon_upgrade_unlock_3_name  drillrobot_au_name  photofilter_nes
trs_bottle_name  rusted_mknight_name  canrobot_au_name  prs_note_2_name
trs_bottle2_name  wep_rustysword_shop_name  key_shopupgrade_4_name  cha_item_energywep_name
au_areaname  trs_bottle3_name  tp_desc_lib  tp_desc_ros
photofilter_angel  acc_caputring_name  sinbook0005_name  tp_desc_aut
modal_apply  option_applyresolution  options_video_applyquestion  lizer_aq_name
magic_cuttledog_name  lost_librarian_desc  inha_j_01_name  inha_j_02_name
inha_j_03_name  titlescreen_deletegame2  d_wourha_00  jrnl_knt2_name
libbook0008_name  jigekitemple_02_desc  options_aspectratio  cocker_gr_desc
stat_attackpower  hud_statup_attack  playerattribs_attackspeed  hud_statup_attackspeed
guide_guns_00_desc  lib0006_desc  options_audio  options_audio_title
spiker_ca_name  ratty_gold_name  options_aspectratio_auto  option_game_autoconsumables
option_game_autoputawayitem  wepstats_speed_average  d_scientist_allkeys_r_02  josami_ca_desc
crawler_ma_name  wepstats_type_spectral  wep_magictorch_name  binding_b
binding_ns_fbbottom  binding_xb_fbright  item_b33punch_name  photofilter_bw
buttonhint_back  binding_backspace  item_cure_heavy_name  upgrade_ultracharge_name
ribber_aq_name  d_sipeha_endchoice_1  barnacle_aq_name  ros_save_start
d_imba_02  cha_bosses_giantfish_name  item_berserk_potion_name  d_king_00
manta_sz_name  bigrobot_ca_name  black_bigrobot_name  pausemenu_bestiary
hud_bestiaryupdated  d_budaen_01  status_bound  playerattribs_bindingres
acc_foodbind_desc  acc_xpbind_desc  acc_wxpbind_desc  option_controls_consolebindings
wep_ow_spikedclub_name  wep_bird_flail_name  cha_quest_chef_name  wep_finalclaw_name
bone_sz_2_name  bone_sz_3_name  bone_sz_name  bone_mage_name
wep_rusted_greataxe_shop_name  trw_poison_dagger_desc  cha_item_secretsword_name  d_scientist_ghost_01
josami_deserter_1_name  key_josamired_name  redjosamilord_gun_name  redjosamilord_sword_name
redjosamilord_group_name  acc_vampireamulet_name  josami_deserter_2_name  josami_deserter_team_name
josami_deserter_4_name  josami_deserter_3_name  key_sz_door_red_name  wep_vampirehammer_name
photomode_blue  wep_b0blade_name  d_skulldoor_gavereward_03  trw_ow_weaken_dagger_name
bone_machine_name  book_bonemachine_name  lost_librarian_name  hero_bone_desc
hero_grave_desc  bookbug_lb_name  bookbug_sz_2_desc  bookbug_lb_3_desc
option_borderless  big_spiker_name  d_budaen_02  gold_drillrobot_name
key_brasschest_name  trs_magicwatch2_name  item_berserk_fruit_name  fish_aq_2_desc
option_video_brightness  photomode_brightness  upgrade_ultracharge_desc  cha_quest_chef_desc
lib0000_desc  secretsword_error_missingstuff  cha_quest_fish_desc  cha_quest_shop_unlocks_desc
upgrade_damage_name  wep_rusted_morningstar_shop_name  acc_attack_1_name  jrnl_knt4_name
d_scientist_gavekey_02  status_azureflame  guide_ultramove_00_desc  aq_explorers_a_02_desc
binding_c  d_en_ghost_02  d_en_intro_00  d_en_postquest_01
trw_fireball_desc  modal_cancel  crawler_lb_name  tpblocked_crystal
tpblocked_travel  binding_capslock  green_josamilord_desc  swmp_horn_jrnl_2_name
d_josami_des_talk_2  photofilter_carrot  royal_team_name  cas_servnote_2_name
wep_geometrizer_bat_desc  aqr_save_cave  item_cure_weaken_desc  wepstats_cellcost
trs_cellphone_name  gr_save_cemetery  key_fishmaidentiara_name  wep_cestus_name
trw_chakram_name  trw_chakram_desc  hud_challengecomplete  pausemenu_challenges
gargler_sz_name  upgrade_attackspeed_desc  trs_jar_name  skull_ma_name
credits_rec_19  credits_rec_15  credits_rec_3  credits_rec_1
credits_rec_0  credits_rec_11  credits_rec_16  credits_rec_17
credits_rec_20  credits_rec_26  credits_rec_18  credits_rec_5
credits_rec_2  credits_rec_13  credits_rec_29  credits_rec_4
credits_rec_8  credits_rec_10  credits_rec_7  credits_rec_14
credits_rec_23  credits_rec_28  credits_rec_22  credits_rec_21
credits_rec_27  credits_rec_24  credits_rec_12  credits_rec_9
credits_rec_25  credits_rec_6  jrnl_chf_name  cocker_gr_name
acc_chilldogamulet_name  ribber_gr_name  credits_chineseloc  trs_jigeki_citrine_name
upgrade_energyrate_desc  cha_quest_meat_desc  item_cure_binding_name  buttonhint_clearbinding
frs_save_1  photofilter_bwboy  modal_close  key_towerdoor_name
fish_aq_name  cha_item_specsword_desc  cha_item_gotalllenses_desc  cha_item_gotallacc_desc
cha_item_gotallweps_desc  cha_item_gotalltreasures_desc  cha_item_gotallthrowables_desc  cha_item_gotallcells_desc
cha_item_flyingsword_desc  d_necronomicon_talk_1  queen_talk_1  jrnl_knt_name
weapon_upgrade_unlock_7_name  wep_rustyspear_shop_name  cha_quest_skulls_desc  journal_enemycompleterate
cha_challenge_allsecretdoors_name  swmp_cultnote_name  option_controls_bindingsbutton  buttonhint_confirm
options_video_confirmchanges  cas_abbhint_name  ancientcity_note_name  pausemenu_continue
photomode_contrast  options_controls_title  options_bindings_title  controller_disconnected
controller_disconnected_ps  options_controls  bone_green_name  crawler_lb_2_name
intro_line_05  titlescreen_credits  prs_save_start_lb  options_crosshairmode
acc_fishwoman_reward_name  whale_king_desc  swamp_save_2  maus_save_upper
photofilter_crunch  wep_crushinglance_name  guide_savepoint_00_name  wep_harpo_club_name
bookbug_lb_3_name  teleport_currentlocation  trs_lizer_7_name  black_skull_name
gr_kknote_desc  photofilter_cyan  binding_d  binding_gp_xb_dpad_down
binding_gp_xb_dpad_left  binding_gp_xb_dpad_right  binding_gp_xb_dpad_up  key_daggers_unlock_name
whale_king_name  wep_ow_spear_name  playerattribs_damageres  guide_damagetypes_00_name
d_aganmi_postnecklaces_01  book_dams_name  jrnl_knt7_name  d_daon_00
bat_sz_name  weapon_upgrade_unlock_4_name  cha_area_prison_name  key_aqr_triangle_d0_name
runecharm_sci_name  d_evil_fishwife_talk_2  swamp_save_1  cha_area_swamp_name
lizer_au_name  photofilter_default  cha_bosses_redlords_desc  cha_bosses_whale_desc
cha_bosses_rosdragon_desc  cha_bosses_bridgelord_desc  cha_bosses_demons_desc  cha_bosses_necro_desc
cha_bosses_dragonlord_desc  cha_bosses_giantfish_desc  missingenemyinbestiary  journal_enemystat_defensetitle
maus_areaname  grtablet_maus00_name  blood_pool_name  binding_delete
buttonhint_titledeletesave  trs_ritualskull2_name  trs_ritualskull_name  cha_bosses_demons_name
tp_desc_forest  big_ballooner_name  fishman_au_desc  swmp_horn_jrnl_1_name
wep_detestableclub_name  wep_diamondblade_name  guide_damagetypes_00_desc  options_screenshake
credits_6  status_diseased  playerattribs_diseaseres  trw_super_block_desc
trs_monocle_name  monacle_gargler_name  golem_au_name  binding_down
binding_ns_down  binding_gp_dpad_down  green_josamilord_name  cha_bosses_rosdragon_name
wep_eyepea_syringe_name  item_heal_herb_name  drillrobot_ca_name  black_drillrobot_name
journal_enemystat_dropstitle  fated_wheeler_name  demon_spider_desc  demon_spider_name
lordly_spiker_name  wep_dustyclub_name  binding_e  journal_enemystat_xp
playerattribs_xptonext  cha_endings_4_name  d_sipeha_endchoice_2  cha_item_gotallacc_name
option_audio_effectvolume  runecharm_egg_name  memento_knight_name  trs_compass_name
item_heal_potion_name  cha_challenge_allchests_name  status_emptyslot  savefile_emptyfilelabel
jrnl_crw_name  option_controls_enablegamepad  photomode_enablesteamscreenshots  option_video_texturestreaming
trs_jar2_name  binding_end  cha_area_lib_name  key_swordshrine_name
options_enemyhp  upgrade_energycap_name  guide_guns_00_name  options_languagename
wep_halberd_name  evil_queen_2_name  binding_enter  trw_heavy_dagger_name
mimic_sz_name  wep_magicheavysword_name  modal_equip  modal_equipaccessory
modal_equiptrwwep  option_binding_equipthrowweapon1  option_binding_equipthrowweapon2  option_binding_equipthrowweapon3
modal_equipwep  option_binding_equipweapon1  option_binding_equipweapon2  option_binding_equipweapon3
guide_equip_00_name  royal_fishman_desc  royal_fishman_name  aqr_save_fishwife
memento_fishwife_name  aq_ermaprisonnote_name  jrnl_fsw_name  binding_escape
photofilter_eve  d_budaen_ghost_01  lib_exc_note_1_name  lib_exc_note_2_name
lib_exc_note_3_name  d_skulldoor_reward_05  teleport_exit  shop_exitbuttonlabel
weaponupgrade_exit  cha_quest_vaults_desc  wep_ow_orbiterhammer_name  cha_item_gotallweps_name
acc_librarian_reward_name  wepstats_speed_exslow  trw_needle_desc  looker_dek_name
binding_f  cha_bosses_redlords_name  key_radiernecklace_name  shop_save_1
blood_manta_desc  lizer_bomb_sz_name  wepstats_speed_fast  cha_quest_raider_name
wheeler_ca_name  d_arjaot_03  wep_byrdseede_sword_name  cha_item_alldashgems_name
drgfly_swamp_name  option_video_fov  systemmessage_saved  titlescreen_fileselecttitle
trs_lizer_1_name  cha_item_energywep_desc  intro_line_10  trs_lizer_6_name
d_skulldoor_reward_01  au_sinkeepernote_name  nude_gargler_name  acc_strangemushroom_name
cha_item_flyingsword_name  d_erma_ghost_01  lib_save_archives  key_libraryarchives_name
looker_gr_name  golem_sz_name  cha_item_secretsword_desc  wep_flyingsword_desc
d_skulldoor_r_02  prison_areaname  cha_quest_vaults_name  mossy_mknight_name
trs_genielamp_name  acc_anemoneartifact_name  acc_fishartifact_name  cs_save_start
lib_save_foyer  option_video_framecap  credits_frenchloc  item_heal_herb_2_name
guide_teleporters_00_desc  option_fullscreen  polter_lb_desc  magic_chickilus_name
binding_g  gameover_loctext  wep_cookingpot_name  memento_chefandcrow_name
option_controls_gamepadbindings  option_controls_gamepadstyle  options_gameplay  options_gameplay_title
photofilter_gamerchair  gargler_lb_2_name  gargler_lb_name  lib0004_name
lib0005_name  lib0006_name  lib0000_name  acc_weakenresist_name
status_gemcurse  cha_area_aut_name  credits_germanloc  d_umbada_oncounter
ghost_fish_name  manticore_au_name  wep_ghostspear_name  bat_ma_name
spider_ma_name  necro_eye_desc  giant_fish_name  wep_rusted_leafblade_shop_name
gold_slime_name  trs_pendant2_name  wep_kinghammer_name  memento_king_name
wep_kingspear_name  seaslug_aq_name  lizer_sz_name  cha_item_gotalltreasures_name
necro_eater_desc  d_ekonta_03  necro_demon_desc  cha_endings_3_name
trs_royalchicken_gold_name  trs_goldslime_crown_name  trs_jewelfish_gold_name  trs_jigeki_gold_name
trs_royalfish_gold_name  trs_puwema_gold_name  trs_gold_ratty_name  trs_royalribber_gold_name
trs_sipeha_gold_name  trs_gold_lizer_name  trs_wodaso_gold_name  trs_fancybell2_name
trs_gcrab_name  key_shopupgrade_2_name  trw_king_dagger_desc  golem_ma_name
wep_golemarm_name  d_skulldoor_reward_03  d_wourha_meatclear_00  bookbug_lb_2_name
item_heal_potion_3_name  looker_ma_name  hero_grave_name  trw_disease_dagger_name
wep_scythe_name  lizer_ma_name  swmp_horn_jrnl_2_desc  wep_josamigreatsword_desc
photomode_green  trw_grenade_name  upgrade_attackspeed_name  wep_yusk_club_name
key_grimoireink_name  acc_xp_name  pausemenu_guides  hud_guideadded
binding_h  d_gagi_intro_00  option_video_hud  wep_haggard_dagger_name
grsz_save  cs_save_junk  cha_area_castle_name  wep_simpleaxe_name
wep_woodclub_shop_name  drgfly_aq_name  option_audio_headphonemode  hud_statup_healing
status_heavy  playerattribs_heavyres  wep_heavyriflegun_name  shop_ownedlabel
cha_bosses_necro_name  demosplash_message  guide_intro_00_desc  ripgrandmatext
cha_quest_lib_desc  wep_hempiesvt38_name  ros_dragon_name  lib0003_desc
wep_poisonpiercer_desc  photomode_hideoptions  photomode_hideoverlay  trw_tech_dagger_name
option_video_pixelfont  black_robot_team_name  weapon_upgrade_unlock_5_name  option_controls_interactrun
modal_confirmdeletehold  key_tridentkey_5_name  trw_spikeball_desc  trs_holybone_name
weapon_upgrade_unlock_1_name  upgrade_ultraunlock2_name  upgrade_ultraunlock2_desc  guide_ultramove_00_name
guide_flyingsword_00_name  wep_flyingsword_name  wep_flingsword_shop_name  binding_home
fishman_aq_name  fishman_aq_3_name  d_arjaot_00  weapon_upgrade_unlock_9_name
acc_knight_name  acc_kingscrest_name  d_whaleking_talk_1  wep_dustyclub_desc
d_arjaot_02  d_arjaot_ghost_01  d_scientist_gavekey_08  book_cook_name
acc_jaybook_name  bat_big_name  binding_i  d_scientist_gavekey_10
d_erma_01  d_gagi_gavereward_03  lib_brroofhint_desc  aq_ermaprisonnote_desc
d_wourha_01  prs_note_1_desc  d_aganmi_r_00  d_umbada_nosell
d_imba_ghost_01  d_sgwhaleking_talk_2  d_nu_00  d_shrineboss_talk_1
d_king_ghost_01  d_nu_ghost_01  d_raider_2_ghost_01  d_erma_posteggs_01
d_gagi_ghost_00  d_daon_ghost_01  d_ekonta_02  d_king_02
looker_rotted_desc  au_sinkeepernote_desc  d_aganmi_allnecklace_02  d_scientist_ghost_02
d_peha_ghost_00  d_ippibi_01  d_necronomicon_talk_2  cas_abbhint_desc
d_wourha_gavereward_04  d_raider_1_ghost_01  d_erma_ghost_02  d_wourha_gavereward_03
d_gagi_postquest_01  d_scientist_postkey_01  cha_quest_books_name  d_peha_r_00
d_gagi_handin_01  d_ippibi_02  item_cure_sticky_name  d_aganmi_r_01
d_budaen_03  d_umbada_behindcounter  jrnl_knt8_name  journal_enemystat_immuneres
dmgtype_imp  wep_reborn_club_name  spiker_ma_name  prs_note_1_name
weaponupgrade_needslevelling  d_wourha_ghost_02  powerterminal_override  gr_kknote_name
cha_quest_grimoire_desc  binding_insert  buttonhint_inspect  trwwepspeed_instant
lib_brroofhint_name  option_binding_interact  guide_savepoint_00_desc  guide_intro_00_name
option_controls_invertconsolex  option_controls_invertgamepadx  option_controls_invertmousex  option_controls_invertconsoley
option_controls_invertgamepady  option_controls_invertmousey  jrnl_knt3_name  d_ekonta_04
fishwife_evil_name  wep_rocketlauncher_name  item_iron_fruit_name  iron_lizer_name
key_raiderchest_name  gr_areaname  d_wourha_ghost_00  d_gagi_gavereward_01
aq_explorers_a_03_desc  inspect_lockedlever  d_aganmi_allnecklace_04  d_skeletonmage_talk_2
binding_j  key_sz_door_green_name  trs_puwema_jade_name  credits_japanloc
wep_jarhammer_name  acc_magicpoisonresist_name  wheeler_black_name  trs_royalfish_silver_desc
trs_royalribber_silver_desc  trs_royalchicken_silver_desc  photofilter_jigeki  key_jigeki_3_name
key_jigeki_2_name  key_jigeki_1_name  frst_jigekishrine_name  swmp_bigfish_name
runevision_unlock_4_name  wep_jigeki_morningstar_name  wep_rustyspear_evo_name  josami_ca_name
trs_josamiskull_name  wep_josamigreatsword_name  josami_lord_au_name  wep_josamimace_name
josami_ca_2_name  journal_title  option_binding_jump  d_wourha_postquest_01
d_gagi_02  d_erma_allegg_01  binding_k  lib_fghint_desc
sinbook0007_name  key_raiderchest_desc  prison_1f_key_desc  prison_2f_key_desc
key_libraryarchives_desc  option_controls_keybindings  jrnl_kng_name  catfishman_au_name
au_rh_jrnl_name  wep_lightningzwei_name  gr_save_knightcamp  key_knightschest_name
jrnl_lib_name  slime_lb_desc  book_gaystuff_name  binding_l
binding_ns_lbutton  binding_ps_lbutton  binding_ps_ltrigger  binding_ps_l3
binding_xb_lbutton  credits_lqa  binding_xb_ltrigger  frs_save_2
wep_harklamp_name  options_languageselection  trw_light_name  item_heal_potion_2_name
jigekitemple_04_desc  d_peha_ghost_02  lizer_lb_name  menuentry_leave
binding_left  binding_leftalt  binding_ns_left  binding_leftctrl
binding_gp_dpad_left  binding_leftmouse  binding_leftshift  binding_l_analog
binding_l_analog_ns  binding_l_analog_ps  binding_l_analog_down  binding_l_analog_ns_down
binding_l_analog_ps_down  binding_l_analog_left  binding_l_analog_ns_left  binding_l_analog_ps_left
binding_l_analog_ns_right  binding_l_analog_ps_right  binding_l_analog_right  binding_l_analog_ns_up
binding_l_analog_ps_up  binding_l_analog_up  cha_quest_shop_spend_name  d_gagi_ghost_02
playerattribs_level  hud_levelup  status_weaponlevel_label  acc_hp_name
hud_statup_health  stat_lifepoints  aqr_save_lift  cs_save_lift
trs_pendant_name  cha_quest_pg_puzzle_name  stone_ribber_name  stone_crawler_name
stone_fishman_name  stone_bone_name  stone_spider_name  golem_sz_desc
buttonhint_titleloadbackup  titlescreen_loadgame  credits_loc  option_controls_mouselock
ros_grimhint_book_desc  photomode_look  option_controls_lookacceleration  option_binding_lookdown
option_binding_lookleft  option_binding_lookright  option_controls_lookspeed  option_binding_lookup
bookbug_sz_name  whale_king_sz_name  mknight_sz_name  photofilter_love
maus_save_lower  playerattribs_luck  hud_statup_luckres  wep_lunaspear_name
binding_m  wep_cyberhammer_name  weapon_upgrade_unlock_2_name  cha_item_gotalllenses_name
playerattribs_magicrate  hud_statup_magicrate  status_magicpoison  playerattribs_magicpoisonres
guide_magicwep_00_name  guide_magicwep_00_desc  green_dragon_name  wep_manroomcross_name
manticore_lib_name  manticore_sz_name  d_ekonta_01  d_skulldoor_reward_02
acc_runecharm_name  tp_desc_maus  option_audio_mainvolume  intro_line_11
maus0001_desc  runecharm_food_name  key_foodticket_name  mknight_ca_name
trs_mecharm_name  key_shopupgrade_3_name  jrnl_sci_name  spider_sz_name
wep_josamimace_desc  item_heal_herb_desc  item_heal_herb_2_desc  drgfly_gr_name
guide_melee_00_name  guide_melee_00_desc  wep_poisonpiercer_name  item_mementomirror_name
cha_area_sg_name  maus0001_name  d_ekonta_ghost_03  binding_xb_spb_right
silver_slime_name  lib_teleporter  binding_middlemouse  mimic_gr_name
lib0007_name  ancientcity_plac_name  trs_lizer_3_name  book_sipeha_name
wep_morningstar_name  moss_golem_name  cha_item_specsword_name  cha_endings_1_name
binding_mouse  option_controls_mouselook  option_controls_mouselookspeed  option_binding_movebackward
option_binding_moveforward  option_binding_moveleft  option_binding_moveright  trw_throwingduck_name
fishwife_evil_group_name  jigekitemple_01_name  jigekitemple_02_name  jigekitemple_03_name
jigekitemple_04_name  weapon_upgrade_unlock_10_name  sinbook0008_name  option_audio_musicvolume
options_mutewhenunfocused  d_aganmi_ghost_01  d_king_03  d_umbada_giveduck_2_01
d_king_ghost_03  lib0003_name  libbook0010_name  bookbug_sz_2_name
looker_sz_name  trs_mirror_name  trw_bignut_name  binding_n
chickilus_au_name  photomode_navigate  key_radiernecklace_desc  necro_eater_name
necro_demon_name  necro_eye_name  necro_zombie_name  buttonhint_nextcategory
option_binding_nextitem  option_binding_nexttrwwep  option_binding_nextwep  option_controls_buttonstyle_ba
status_noitems  roof_mknight_name  binding_nokey  journal_enemystat_normaldmg
playerattribs_status_normal  wep_b33blade_name  item_b33punch_desc  d_umbada_frontdoor_01
d_peha_03  guide_shop_00_desc  seaslug_sz_name  jrnl_knt5_name
binding_numast  binding_numplus  binding_numhyphen  binding_numdot
binding_numslash  binding_num0  binding_num1  binding_num2
binding_num3  binding_num4  binding_num5  binding_num6
binding_num7  binding_num8  binding_num9  binding_numlock
bestiary_killcount  wep_challengereward_name  binding_o  modal_ok
aqr_areaname  aq_explorers_a_00_name  aq_explorers_a_01_name  aq_explorers_a_02_name
aq_explorers_a_03_name  golem_sz_2_name  sys_challenge_final_sword  trw_homing_dagger_name
trw_homing_dagger_three_name  wep_magicspear_name  wep_magicwarhammer_name  trs_josamiskull2_name
josami_sz_2_name  josami_sz_name  cha_area_aqr_name  fish_au_name
option_toggle_off  d_whaleking_talk_2  d_aganmi_r_02  d_scientist_gavekey_06
d_umbada_giveduck_4_01  cha_quest_mushroom_name  d_aganmi_handin_01  d_scientist_gavekey_09
d_gagi_handin_00  d_scientist_gavekey_03  demosplash_final  trw_magicfire_dagger_name
wep_spectralsword_name  key_rosorb_name  wep_flameaxe_name  photofilter_gboy
option_toggle_on  d_aganmi_ghost_03  tp_desc_swamp  whale_king_sz_desc
d_wourha_gavereward_01  d_erma_gaveegg_01  key_aqr_triangle_0_desc  key_aqr_triangle_d0_desc
upgrade_ultraunlock_desc  upgrade_dashes_desc  book_puwema_desc  trs_sipeha_onyx_name
acc_stickyresist_name  option_binding_pause  photomode_screenshotfolder  option_binding_status
cha_challenge_allchests_desc  cha_challenge_allsecretdoors_desc  menu_options  options_title
binding_ps_spb_right  photofilter_orange  key_officedoor_name  key_fishegg_name
acc_fish_name  item_resist_mp_potion_name  deer_sz_name  au_save_sci
memento_scientist_name  weapon_upgrade_unlock_8_name  photofilter_ourple  cha_all_complete_desc
cha_item_gotallcells_name  au_save_start  binding_p  binding_pagedown
binding_pageup  canrobot_ca_name  fish_sz_name  looker_ma_desc
photomode_pallete  item_cure_potion_name  wep_pandorablade_name  trw_para_dagger_name
status_paralyze  lib_fghint_name  binding_pause  pausemenu_title
option_game_pausewhenunfocus  option_controls_pausebinderror  photofilter_pear  wep_pebblehammer_name
jrnl_bmm_name  memento_bmmc_name  acc_vampireamulet_desc  libbook0007_name
cha_item_upgradeallweps_name  d_skulldoor_gavereward_02  ballooner_sz_name  sg_areaname
photomode_name  dmgtype_abs  dmgtype_prc  wep_snipergun_name
trw_needle_name  trw_needle_three_name  item_pigeonfossil_name  maus_pilgrimnote_1_name
maus_pilgrimnote_2_name  maus_pilgrimnote_3_name  ros_save_end  photofilter_piss
prs_save_start_gr  trw_energydagger_name  wep_energyspear_name  wep_energysword_name
titlescreen_playgame  saveslot_playtime  option_controls_buttonstyle_xo  d_scientist_r_01
item_berserk_fruit_desc  trs_rattyplush_name  looker_gr_desc  demon_group_name
demon_bat_desc  demon_bat_name  status_poison  trs_rotchalice2_name
playerattribs_poisonres  trw_poison_dagger_name  trw_poison_dagger_two_name  polter_lb_name
credits_7  credits_brazilloc  au_genhint_name  powerterminal_title
wepstats_power  acc_wxpbind_name  options_binding_gamepad_label  options_binding_console_label
options_binding_ns_label  options_binding_ps_label  options_binding_label  modal_videoconfirm
binding_ns_l3  binding_xb_l3  binding_ns_r3  binding_xb_r3
buttonhint_prevcategory  option_binding_previtem  option_binding_prevtrwwep  option_binding_prevwep
option_binding_attack  hod_shrine_hint_name  intro_line_09  d_imba_01
end4text_01  prs_note_2_desc  aqr_save_prison  credits_projectmanager
acc_defense_name  credits_5  item_cure_disease_name  necro_zombie_sz_name
d_umbada_buy_option  shop_purchaseformat  slime_lb_name  cocker_sz_name
photofilter_puwema  wep_woodclub_evo_name  acc_regen_name  runevision_unlock_1_name
maus_save_puwema  pt_areaname  wep_puwema_rapier_name  binding_q
credits_2  credits_qaspec  credits_8  d_skulldoor_gavereward_01
trs_wodaso_quartz_name  evil_queen_name  tp_desc_castle  josami_lord_au_desc
cha_all_complete_name  acc_energyrate_name  titlescreen_quit  pausemenu_quittodesktop
pausemenu_quitdeskconfirm  pausemenu_quittomenu  pausemenu_quitmenuconfirm  d_wourha_reward_02
d_wourha_noreward_01  binding_ns_rbutton  binding_r  binding_ps_rbutton
binding_ps_rtrigger  binding_ps_r3  binding_xb_rbutton  binding_xb_rtrigger
wep_plasmagun_name  item_glowfruit_name  wep_hempie_sword_name  berserk_lizer_name
cha_item_ultragem_name  jrnl_rdc_name  trs_lizer_2_name  item_heal_herb_3_desc
wep_luude_crossbow_name  item_ratpod_name  ratty_gr_name  book_rattybug_name
seaslug_big_name  wep_rustysword_evo_desc  cha_area_castle_desc  cha_area_aut_desc
cha_area_maus_desc  cha_area_prison_desc  cha_area_aqr_desc  cha_area_sg_desc
cha_area_ros_desc  cha_area_swamp_desc  cha_area_lib_desc  cha_area_forest_desc
bone_sz_3_desc  bone_sz_2_desc  bone_sz_desc  cha_quest_mushroom_desc
ros_areaname  cha_item_alldashgems_desc  cha_item_ultragem_desc  photomode_red
fish_aq_2_name  powerterminal_refill  wep_flyingsword_secret_desc  lizer_ca_name
au_genhint_desc  wep_jarhammer_shop_name  item_rejuv_potion_name  modal_removefromquickslots
option_audio_resettodefault  modal_revertaudiomessage  modal_revertallbindings  option_binding_resettodefault
modal_revertbindingmessage  photomode_resetcolor  option_controls_revertall  modal_revertcontrolsmessage
option_game_revertall  modal_revertgameplaymessage  powerterminal_reset  modal_videorevert
photomode_resetposition  option_video_revertall  modal_revertvideomessage  journal_enemystat_lessdmg
option_video_resolution  option_video_resscale  menuentry_rest  wep_rustysword_shop_desc
option_video_retrores  cha_quest_books_desc  crystaltp_modalbody  wepstats_return
josami_ca_2_desc  binding_right  binding_rightalt  binding_ns_right
binding_rightctrl  binding_gp_dpad_right  binding_rightmouse  binding_rightshift
binding_r_analog  binding_r_analog_ns  binding_r_analog_ps  binding_r_analog_down
binding_r_analog_ns_down  binding_r_analog_ps_down  binding_r_analog_left  binding_r_analog_ns_left
binding_r_analog_ps_left  binding_r_analog_ns_right  binding_r_analog_ps_right  binding_r_analog_right
binding_r_analog_ns_up  binding_r_analog_ps_up  binding_r_analog_up  trw_fireball_name
trw_wind_name  wep_claw_name  blood_manta_name  blood_stalker_name
blood_bullsnail_name  wep_haggard_dagger_desc  trs_compass2_name  item_cure_weaken_name
item_cure_poison_name  upgrade_health_name  buttonhint_rotate  buttonhint_rotateleft
buttonhint_rotateright  trs_rotchalice_name  fishman_sz_name  hellslime_disease_name
swamp_areaname  looker_rotted_name  au_rh_decree_name  item_heal_herb_3_name
trw_king_dagger_name  option_controls_rumblestr  option_binding_run  credits_russianloc
prison_1f_key_name  wep_rusted_morningstar_name  wep_rusted_greataxe_name  wep_rusted_greatsword_name
wep_rusted_leafblade_name  wep_rusted_hammer_name  wep_rustysword_name  wep_rustyspear_name
binding_s  aqr_save_cistern  cha_area_forest_name  acc_allresists_name
cha_quest_fish_name  tp_desc_aq  ratty_au_name  photomode_saturation
wep_saurianwing_name  menuentry_save  saveslot_savedlabel  systemmessage_saving
cha_endings_1_desc  cha_endings_2_desc  cha_endings_3_desc  cha_endings_4_desc
wep_rusted_greatsword_shop_name  mimic_red_name  looker_au_name  au_save_scrap
buttonhint_scrolldescription  binding_scrolldown  binding_scrolllock  buttonhint_scroll
binding_scrollup  wep_magicalmartini_scythe_name  key_castle_cup_name  key_castle_pentacle_name
key_castle_sword_name  key_castle_wand_name  option_binding_secondaryattack  ros_dragon_talk_1
options_video_failedtoapply  d_umbada_sell_option  shop_sellformat  cuttledog_au_name
photofilter_serious  d_king_01  wep_elecgun_name  menuentry_shop
option_bestiarynotifs  option_challengenotifs  options_game_showcompass  option_guidenotifs
options_crosshairmode_showinteract  photomode_showoptions  hero_bone_name  crawler_sz_name
acc_trw_cooldown_name  trs_fancybell_name  trs_royalchicken_silver_name  trs_metalslime_crown_name
key_shopupgrade_1_name  d_skulldoor_r_00  trs_jewelfish_silver_name  trs_jigeki_silver_name
wep_rusted_hammer_shop_name  trs_royalfish_silver_name  wep_needlerapier_name  trs_magicwatch_name
trs_puwema_silver_name  trs_silver_ratty_name  trs_royalribber_silver_name  trs_sipeha_silver_name
trs_silver_jewel_lizer_name  trs_wodaso_silver_name  ros_dragon_desc  cha_area_ros_name
sinkp_jrnl_name  wep_greatspear_name  photofilter_sipeha  runevision_unlock_3_name
wep_rustysword_evo_name  wep_sipeha_hammer_name  cs_areaname  weapon_upgrade_unlock_6_name
sinbook0009_name  boner_aq_2_name  bone_gr_2_name  executioner_bone_name
bone_gr_name  boner_aq_name  boner_aq_3_name  item_cure_poison_desc
pausemenu_skipcutscene  pausemenu_skipcutscenewarning  runecharm_skull_name  wep_skullrod_name
dmgtype_sls  d_evil_fishwife_talk_1  trs_lizer_5_name  wepstats_speed_slow
upgrade_energycap_half_name  tp_desc_gr  fish_sz_desc  d_ippibi_03
d_skulldoor_reward_04  item_cure_sticky_desc  shop_soldtreasurelabel  shop_soldoutlabel
wep_cybersword_name  trw_shop_stone_block_name  acc_windcrystalartifact_name  wep_flyingsword_evo_name
sipeha_name  binding_spacebar  credits_spanishloc  sparkle_bug_name
cha_quest_meat_name  option_binding_specialattack  photofilter_specialcomp  photofilter_spectrum
wepstats_speed  cha_quest_shop_spend_desc  trw_spikeball_name  wep_spikeshield_name
item_wxp_orb_name  jrnl_knt2_spirit_name  jrnl_knt4_spirit_name  jrnl_chf_spirit_name
jrnl_knt_spirit_name  jrnl_knt7_spirit_name  jrnl_crw_spirit_name  jrnl_fsw_spirit_name
jrnl_knt8_spirit_name  jrnl_knt3_spirit_name  jrnl_kng_spirit_name  jrnl_lib_spirit_name
jrnl_sci_spirit_name  jrnl_knt5_spirit_name  jrnl_bmm_spirit_name  jrnl_rdc_spirit_name
jrnl_rd3_spirit_name  jrnl_rd4_spirit_name  jrnl_rd2_spirit_name  jrnl_rd1_spirit_name
jrnl_knt6_spirit_name  sinbook0004_name  wep_aquahammer_name  acc_xpbind_name
lib_save_stairs  trs_gold_wodasocross_name  stalker_sz_name  trw_basic_dagger_desc
runecharm_lizer_name  lizer_gr_name  wep_saosword_name  titlescreen_newgame
acc_foodbind_name  stone_ribber_desc  stone_crawler_desc  stone_spider_desc
stone_fishman_desc  stone_bone_desc  playerattribs_status  pausemenu_status
guide_status_00_name  upgrade_energyrate_name  item_iron_potion_name  option_controls_deadzone
option_controls_deadzone_ns  option_controls_deadzone_ps  status_sticky  playerattribs_stickyres
shop_stocklabel  key_shopupgrade_0_name  wep_duckhammer_name  sg_save_hut
upgrade_ultraunlock_name  photofilter_strange  orbiter_name  key_secretskull_name
trw_stone_block_name  jrnl_skulldoor_name  lib_woodpanel_name  acc_attack_name
evil_queen_2_desc  wep_tridentkeytrident_shop_name  barnacle_sz_name  d_skulldoor_r_01
d_necronomicon_intro_2  upgrade_healing_name  d_erma_r_01  d_wourha_ghost_01
pausemenu_suspenddeskconfirm  pausemenu_suspendmenuconfirm  status_swapaccessorybutton  status_swapdaggerbutton
option_controls_swapanalog  option_controls_swapanalog_ns  option_controls_swapanalog_ps  wep_puwema_rapier_desc
d_skulldoor_gavereward_04  cha_bosses_bridgelord_name  trs_jarhammer_treasure_name  binding_t
binding_tab  gr_mghint_desc  photomode_takephoto  lib0004_desc
d_erma_gavereward_02  d_peha_02  option_controls_taptorun_console  option_controls_taptorun_gamepad
option_controls_taptorun  prison_2f_key_name  bullsnail_sz_name  key_forbiddendoor_name
credits_teamlead  cha_quest_raider_desc  swamp_save_3  credits_lqalead
d_en_ghost_00  credits_3  guide_dagger_00_desc  d_budaen_00
d_ujji_ghost_00  d_ippibi_ghost_01  d_erma_gavereward_01  d_wourha_r_01
d_aganmi_allnecklace_01  d_shrineboss_talk_2  d_gagi_gavereward_02  intro_line_08
cha_quest_skulls_name  d_gagi_r_00  sinbook0000_name  nodaggerbelt_tw_message
book_war_name  book_alchemy_name  book_puwema_name  book_dragon_name
guide_flyingsword_00_desc  wep_flyingsword_evo_desc  libbook0006_name  libbook0012_name
libbook0005_name  necronomicon_name  photofilter_newflesh  book_eldritch_name
sinbook0006_name  book_jigeki_name  libbook0004_name  shop_unlockinspect
wep_ow_orbiterhammer_desc  ros_grimhint_book_name  intro_line_02  kingsword_remnant_desc
boner_aq_2_desc  boner_aq_desc  boner_aq_3_desc  bone_mage_desc
sinbook0007_desc  key_jigeki_3_desc  skull_ma_desc  turned_knight_desc
trs_goldslime_crown_desc  trs_metalslime_crown_desc  wheeler_black_desc  au_rh_jrnl_desc
intro_line_04  wheeler_ma_desc  d_necronomicon_text_1  door_wrongside
door_needsscriptopen  aqr_bossdoor_locked  d_scientist_01  d_erma_gavereward_04
whale_prince_desc  item_pigeonfossil_desc  acc_strangemushroom_desc  acc_anemoneartifact_desc
acc_fishartifact_desc  item_iron_potion_desc  door_gatelocked  d_umbada_boughtalot_02
fishwife_evil_desc  grimoire_invalidcode  keymenu_defaulterror  key_knightschest_desc
trs_dreamstation_desc  trw_bignut_desc  d_ekonta_06  d_erma_r_03
weaponupgrade_none_available  d_largerustrobot_talk_1  key_jigeki_2_desc  aqr_sz_greenroomnote_1_desc
d_aganmi_01  grimoire_outofink  au_rh_decree_desc  executioner_bone_desc
d_josami_des_talk_1  wep_saosword_desc  d_erma_02  d_orbiter_talk_1
wep_harklamp_desc  key_jigeki_1_desc  sipeha_desc  bone_gr_2_desc
bone_gr_desc  bone_ma_2_desc  bone_ma_desc  bone_ma_3_desc
d_raider_4_ghost_01  guide_status_00_desc  d_wourha_r_00  d_scientist_gavekey_01
d_scientist_02b  modal_unappliedchanges  d_wourha_reward_01  golem_au_desc
acc_verhomask_name  options_fontoption  wep_wavegun_desc  sinbook0006_desc
item_grimoire_desc  libbook0008_desc  book_wodaso_desc  chickilus_au_desc
wep_jigeki_morningstar_desc  d_wourha_reward_04  d_ekonta_05  wep_wodaso_greatsword_desc
d_scientist_gavekey_04  wep_b33blade_desc  trw_wind_desc  wep_kingspear_desc
d_scientist_handin_02  crawler_lb_2_desc  crawler_lb_desc  crawler_ma_desc
slime_sz_name  d_arjaot_01  jigekitemple_03_desc  trw_axe_name
trw_basic_dagger_name  guide_dagger_00_name  binding_thumbmouse1  binding_thumbmouse2
photomode_tilt  hod_shrine_hint_desc  guide_equip_00_desc  gr_trshnt_2_desc
d_sgwhaleking_talk_1  playerattribs_totalxp  binding_ps_spb_left  manta_ca_name
acc_diseaseresist_name  lib_areaname  grtablet_lib00_name  fishman_aq_2_name
wk_fish_name  jrnl_mer_name  acc_wepxp_name  item_inf_returngem_name
item_returngem_name  cha_bosses_whale_name  whale_prince_name  runecharm_chest_name
gr_mghint_name  gr_trshnt_2_name  aqr_sz_greenroomnote_1_name  key_aqr_triangle_0_name
wep_tridentkeytrident_name  key_tridentkey_3_name  key_tridentkey_1_name  key_tridentkey_4_name
key_tridentkey_2_name  key_tridentkey_0_name  wep_flyingsword_secret_name  binding_u
wepstats_upgradedlabel  jrnl_knt6_name  turned_knight_name  shop_areaname
key_merchant_chest_name  guide_shop_00_name  key_merchant_door_name  d_raider_3_ghost_01
binding_unbound  sz_areaname  modal_unequip  deer_au_name
trs_holybone2_name  kingsword_remnant_name  d_wourha_02  cha_area_maus_name
binding_up  binding_ns_up  binding_gp_dpad_up  weaponupgrade_complete
cha_item_upgradeallweps_desc  weaponupgrade_purchaseformat  book_fairytales_name  catfishman_au_desc
jigekitemple_01_desc  modal_use  option_binding_useitem  guide_useitem_00_name
option_binding_throwdagger  guide_useitem_00_desc  dragonlord_team_name  binding_v
option_video_vsync  trs_mirror2_name  trw_vampire_dagger_name  wep_vampirescimitar_name
key_sz_door_blue_name  bone_ma_2_name  bone_ma_3_name  bone_ma_name
au_droptunnel_save  frs_areaname  fishman_au_name  cha_item_gotallthrowables_name
versionnumberformat  wepstats_speed_veryfast  wepstats_speed_veryslow  yellowjacket_deer_name
upgrade_dashes2_name  option_controls_vibrationstr  options_video  options_video_title
binding_xb_spb_left  gargler_sz_2_name  trs_genielamp2_name  fish_sz_2_name
trw_vineshroom_name  wep_geometrizer_bat_name  photofilter_virtual  slime_au_name
photofilter_vomit  wep_voxord_name  binding_w  cha_bosses_dragonlord_name
cha_quest_pg_puzzle_desc  royal_ribber_desc  royal_ribber_name  libbook0009_name
wander_stalker_name  acc_defense_1_name  wep_wavegun_name  trs_lizer_4_name
d_skulldoor_postquest_01  ancientcity_note_desc  aq_explorers_a_01_desc  aq_explorers_a_00_desc
journal_enemystat_moredmg  status_weak  playerattribs_weakenres  hud_weaponbroke
hud_weaponevo  hud_weaponlevelup  d_gagi_00  grtablet_lib00_desc
cha_challenge_allteleporters_name  d_umbada_00  d_umbada_02  d_umbada_giveduck_3_01
d_umbada_handin  d_skeletonmage_talk_1  wheeler_ma_name  runevision_unlock_4_desc
runevision_unlock_1_desc  runevision_unlock_3_desc  runevision_unlock_2_desc  acc_heavyresist_name
white_tgolem_name  d_scientist_00  d_imba_00  item_ratpod_desc
bone_green_desc  libbook0011_name  wep_slashgun_name  option_binding_winddash
upgrade_dashes_name  upgrade_dashes2_desc  wep_rapier_name  option_video_windowmode
option_windowed  wing_altar_travel  teleport_undiscovered  guide_teleporters_00_name
item_inf_tp_crystal_name  item_tp_crystal_name  trs_dreamstation_name  cha_quest_science_name
item_grimoire_name  wep_raynescythe_name  cha_quest_grimoire_name  end2text_01
end3text_01  intro_line_03  bookbug_sz_desc  d_wourha_gavereward_02
grtablet_maus00_desc  cha_quest_science_desc  photofilter_wodaso  book_wodaso_name
sinbook0003_name  acc_xp_1_name  wep_wodaso_greatsword_name  runevision_unlock_2_name
trw_super_block_name  grave_warden_name  maus_pilgrimnote_3_desc  d_scientist_allkeys_r_01
wep_woodclub_name  lib_exc_note_2_desc  lib_exc_note_3_desc  lib_exc_note_1_desc
memento_librarian_name  lib_save_wourha  redjosamilord_gun_desc  wep_katar_name
binding_ns_fbtop  binding_x  binding_xb_fbleft  option_controls_buttonstyle_xb
binding_ns_fbleft  binding_xb_fbtop  binding_y  wep_reborn_club_desc
royal_cocker_desc  royal_cocker_name  d_skulldoor_allskulls_01  intro_line_07
d_erma_reward_01  postend_awaken_01  postend_awaken_00  d_necronomicon_intro_1
d_scientist_03  wep_b0blade_desc  d_erma_handin_01  empty_keymenu
empty_keymenu_2  shop_notenoughmoney  weaponupgrade_notenoughmoney  queen_break_1
lib0005_desc  d_peha_01  wep_raynescythe_desc  d_scientist_02
d_queen_badend_00  d_josamilord_1  d_erma_r_02  d_wourha_reward_03
d_ekonta_ghost_02  d_scientist_handin_01  d_gagi_reward_01  d_erma_reward_05
d_wourha_handin_01  d_ekonta_00  d_ekonta_ghost_01  d_ippibi_00
secretsword_error_unevod  intro_line_06  d_umbada_boughtalot_01  d_erma_reward_04
d_skulldoor_handin_01  d_skulldoor_00  redjosamilord_sword_desc  binding_z
binding_ns_ltrigger  binding_ns_rtrigger  wep_zerkhander_name  photomode_zoom
binding_leftbracket  binding_backslash  binding_rightbracket  binding_tilde
ritualtext_06  ritualtext_15  ritualtext_12  ritualtext_17
ritualtext_11  ritualtext_03  ritualtext_02  d_skulldoor_gavereward_05
ritualtext_10  ritualtext_14  ritualtext_16  ritualtext_13
ritualtext_05  ritualtext_01  ritualtext_09  ritualtext_07
ritualtext_04  ritualtext_08  pausemenu_quitwarningtimeformat  coinbag_name
numformat_percent  StatusScreen
```
