# A lean control page, the settings explained in a document

## Goal

F0015 put a description of every setting onto the control page. On the owner's production board (a HeishaMon small board, ESP8266) the page then got about twice as large, and production restarted twice while the owner had it open, with the MQTT log running at about nine publishes a second. The likely cause is RAM: upstream's web server holds every piece of a page in RAM until the browser has fetched it. The owner decided: the page goes back to the minimum, and the descriptions move into a document of the repository, linked from the page. Done when the story is merged, `make check` is green, the page carries no descriptions, and `docs/control/settings.md` explains every setting.

## Scope

- The page: no description column and no description text in the firmware; one link to the document.
- A new document `docs/control/settings.md` with every setting.

## Out of scope

- The save result column of F0015 stays: it is short.
- How upstream's web server buffers a page.

## Stories

- S0001: Descriptions into a document

## The human's changes

None.

## Decisions taken for the human

1. The descriptions F0015 wrote, with the corrections its builder made against the code, move into the document as they are; the firmware keeps none of them.
2. The page links to the document on GitHub, branch `develop`.
