// DialogueBacklog: speaker names. _kind 0 = \F face letter, 1 = \T voice letter, 2 = global.fc number,
// 3 = text sound asset name. Returns "" for narration and undefined when the key is unknown.
function scr_drbacklog_name(_kind, _key)
{
    if (_kind == 0)
    {
        switch (_key)
        {
            case "0": return "";
            case "S": return "Susie";
            case "R": return "Ralsei";
            case "N": return "Noelle";
            case "T": return "Toriel";
            case "L": return "Lancer";
            case "s": return "Sans";
            case "U": return "Undyne";
            case "A": return "Asgore";
            case "a": return "Alphys";
            case "B": return "Berdly";
            case "i": return "Catti";
            case "J": return "Jockington";
            case "r": return "Rudy";
            case "k": return "Catty";
            case "y": return "Bratty";
            case "u": return "Rouxls Kaard";
            case "b": return "Burgerpants";
            case "K": return "King";
            case "Q": return "Queen";
            case "C": return "Carol";
            case "F": return "Flowery";
            case "f": return "Flowery";
        }
        return undefined;
    }
    if (_kind == 1)
    {
        switch (_key)
        {
            case "0": return "";
            case "1": return "";
            case "X": return "";
            case "S": return "Susie";
            case "R": return "Ralsei";
            case "N": return "Noelle";
            case "n": return "Noelle";
            case "B": return "Berdly";
            case "T": return "Toriel";
            case "L": return "Lancer";
            case "K": return "King";
            case "s": return "Sans";
            case "U": return "Undyne";
            case "A": return "Asgore";
            case "a": return "Alphys";
            case "r": return "Rudy";
            case "J": return "Jevil";
            case "Q": return "Queen";
            case "q": return "Queen";
            case "p": return "Spamton NEO";
        }
        return undefined;
    }
    if (_kind == 2)
    {
        switch (_key)
        {
            case 1: return "Susie";
            case 2: return "Ralsei";
            case 3: return "Noelle";
            case 4: return "Toriel";
            case 5: return "Lancer";
            case 6: return "Sans";
            case 9: return "Undyne";
            case 10: return "Asgore";
            case 11: return "Alphys";
            case 12: return "Berdly";
            case 13: return "Catti";
            case 14: return "Jockington";
            case 15: return "Rudy";
            case 16: return "Catty";
            case 17: return "Bratty";
            case 18: return "Rouxls Kaard";
            case 19: return "Burgerpants";
            case 20: return "King";
            case 21: return "Queen";
            case 22: return "Carol";
            case 23: return "Flowery";
            case 24: return "Flowery";
            case 25: return "Blue";
        }
        return undefined;
    }
    if (_kind == 3 && is_string(_key))
    {
        switch (_key)
        {
            case "snd_txtsus": return "Susie";
            case "snd_txtral": return "Ralsei";
            case "snd_txtnoe": return "Noelle";
            case "snd_txttor": return "Toriel";
            case "snd_txtlan": return "Lancer";
            case "snd_dadtxt": return "King";
            case "snd_txtber": return "Berdly";
            case "snd_txtsans": return "Sans";
            case "snd_txtund": return "Undyne";
            case "snd_txtasg": return "Asgore";
            case "snd_txtal": return "Alphys";
            case "snd_txtrud": return "Rudy";
            case "snd_txtjok": return "Jevil";
            case "snd_txtq": return "Queen";
            case "snd_txtq_2": return "Queen";
            case "snd_txtspam": return "Spamton";
            case "snd_txtspam2": return "Spamton NEO";
            case "snd_txtsusral": return "Susie & Ralsei";
            case "snd_tv_voice_short": return "Tenna";
            case "snd_txtcar": return "Carol";
            case "snd_txger": return "Gerson";
            case "snd_txtjack_low2": return "Jackenstein";
            case "snd_txtjack_high_cute": return "Jackenstein";
            case "snd_flowery_voicenoise_1": return "Flowery";
            case "snd_flowery_voicenoise_loop": return "Flowery";
        }
    }
    return undefined;
}
