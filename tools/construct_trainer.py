import json

with open('drum_patterns.all.json', 'r') as f:
    data = json.load(f)

with open('drum_patterns.fst.trainer', 'w', newline='\n') as f:
    for genre in data.keys():
        main_parts  = data[genre].get('main')
        break_parts = data[genre].get('break')
        if main_parts:
            for pattern_id, seq in main_parts.items():
                main_trans = f'<eps>|<{genre}>|<{pattern_id}>|' + '|'.join([str(x) for x in seq]) + f'|<{pattern_id}>|<eps>'
                f.write(f'{main_trans}\n')
        else:
            main_trans = f'<eps>|<{genre}>|<eps>'
            f.write(f'{main_trans}\n')
        if break_parts:
            for pattern_id, seq in break_parts.items():
                break_trans = f'<eps>|<{genre}>|<break>|<{pattern_id}>|' + '|'.join([str(x) for x in seq]) + '|<eps>'
                f.write(f'{break_trans}\n')
        else:
            break_trans = f'<eps>|<{genre}>|<break>|<eps>'
            f.write(f'{break_trans}\n')
