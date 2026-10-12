import { Link } from 'react-router';

import { Button } from '@/components/ui/button';

import { SoundsSection } from './Sounds';
import { WallpapersSection } from './Wallpapers';

/** The shared library of Now: the wallpapers behind every screen and the sounds of focus mode. */
export function AdminLibraryPage() {
  return (
    <div className="container mx-auto max-w-4xl py-8 space-y-10">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-3xl font-bold">Library</h1>
          <p className="text-sm text-muted-foreground">
            Wallpapers and focus sounds, shared by every user.
          </p>
        </div>
        <Button asChild variant="ghost">
          <Link to="/admin">← Admin</Link>
        </Button>
      </div>
      <WallpapersSection />
      <SoundsSection />
    </div>
  );
}
