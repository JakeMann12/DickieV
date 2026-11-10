import json, ssl, urllib.request

def fetch(url):
    with urllib.request.urlopen(url, context=ssl._create_unverified_context(), timeout=20) as r:
        return json.load(r)

print('--- SCOREBOARD groups=50 ---')
score = fetch('https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard?groups=50&limit=400')
for ev in score.get('events', []):
    teams=[]
    for comp in ev.get('competitions', []):
        for team in comp.get('competitors', []):
            teams.append(team.get('team', {}).get('abbreviation'))
    if 'DUKE' in teams:
        print('MATCH', ev.get('id'), ev.get('date'), ev.get('status', {}).get('type', {}).get('state'))
        for comp in ev.get('competitions', []):
            print('startDate', comp.get('startDate'))
            for team in comp.get('competitors', []):
                print('team', team.get('team', {}).get('abbreviation'), 'score', team.get('score'))

print('--- SCOREBOARD date=20261110 ---')
score_date = fetch('https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard?dates=20261110')
for ev in score_date.get('events', []):
    teams=[]
    for comp in ev.get('competitions', []):
        for team in comp.get('competitors', []):
            teams.append(team.get('team', {}).get('abbreviation'))
    if 'DUKE' in teams:
        print('MATCH', ev.get('id'), ev.get('date'), ev.get('status', {}).get('type', {}).get('state'))
        for comp in ev.get('competitions', []):
            print('startDate', comp.get('startDate'))
            for team in comp.get('competitors', []):
                print('team', team.get('team', {}).get('abbreviation'), 'score', team.get('score'))

print('--- TEAM SCHEDULE DUKE ---')
sched = fetch('https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/teams/duke/schedule')
for ev in sched.get('events', []):
    print(ev.get('date'), ev.get('status', {}).get('type', {}).get('state'), ev.get('status', {}).get('type', {}).get('shortDetail'))
