To stop git from tracking telegram token (so bot information is not made public) 

git update-index --assume-unchanged include/telegram_token.h

To start tracking again:

git update-index --no-assume-unchanged include/telegram_token.h