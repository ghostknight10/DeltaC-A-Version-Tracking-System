ls = str(input("Enter a message to scramble: "))
ls = ls.replace(' ', '#')
ls = list(ls.split("#"))
# print(ls)
key = int(input("Enter the secret number (key): "))
res = ""
for j in range(len(ls)):
    _word = ""
    for i in range(len(ls[j])):
        t = ord(ls[j][i])
        if ls[j][i].isalpha():
            if j % 2 == 0:
                if (t + key > 90 and t + key < 97) or (t + key > 122):
                    _word += chr(t + key - 26)
                else:   
                    _word += chr(t + key)
            else:
                if t - key < 65 or (t - key > 90 and t - key < 97):
                    _word += chr(t - key + 26)
                else:
                    _word += chr(t - key)
        else:
            _word += ls[j][i]
    res+=_word
    if j != len(ls) - 1:
        res += "#"
print("Scrambled Message:", res)